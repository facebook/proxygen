/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HPACKEncoder.h>

#include <algorithm>
#include <unordered_set>
#include <utility>

using folly::IOBuf;
using std::list;
using std::unique_ptr;
using std::unordered_set;
using std::vector;

namespace proxygen {

HPACKEncoder::HPACKEncoder(HPACK::MessageType msgType,
                           bool huffman,
                           uint32_t tableSize) :
    HPACKContext(msgType, tableSize),
    huffman_(huffman),
    buffer_(kBufferGrowth,
            (msgType == HPACK::MessageType::REQ) ?
            huffman::reqHuffTree05() : huffman::respHuffTree05(),
            huffman) {
}

HPACKEncoder::HPACKEncoder(const huffman::HuffTree& huffmanTree,
                           bool huffman,
                           uint32_t tableSize) :
    // since we already have the huffman tree, msgType doesn't matter
    HPACKContext(HPACK::MessageType::REQ, tableSize),
    huffman_(huffman),
    buffer_(kBufferGrowth, huffmanTree, huffman) {
}

unique_ptr<IOBuf> HPACKEncoder::encode(const vector<HPACKHeader>& headers,
                                       uint32_t headroom) {
  table_.clearSkippedReferences();
  if (headroom) {
    buffer_.addHeadroom(headroom);
  }
  encodeDelta(headers);
  for (const auto& header : headers) {
    if (willBeAdded(header)) {
      encodeEvictedReferences(header);
    }
    encodeHeader(header);
  }
  return buffer_.release();
}

bool HPACKEncoder::willBeAdded(const HPACKHeader& header) {
  auto index = getIndex(header);
  return isStatic(index) || (index == 0 && header.isIndexable());
}

void HPACKEncoder::encodeEvictedReferences(const HPACKHeader& header) {
  uint32_t index = table_.size();
  uint32_t bytes = table_.bytes();
  // the header will be added to the header table
  while (index > 0 && (bytes + header.bytes() > table_.capacity())) {
    // double encode only if the element is in the reference set
    if (table_.isSkippedReference(index)) {
      // 1. this will remove the entry from the refset
      encodeAsIndex(dynamicToGlobalIndex(index));
      // 2. this will add the same entry to the refset and emit it
      encodeAsIndex(dynamicToGlobalIndex(index));
    }
    bytes -= table_[index].bytes();
    index--;
  }
}

void HPACKEncoder::encodeDelta(const vector<HPACKHeader>& headers) {
  // compute the difference between what's in reference set and what's in the
  // reference set

  list<uint32_t> refset = table_.referenceSet();
  // what's in the headers list and in the reference set - O(N)
  vector<uint32_t> toEncode;
  toEncode.reserve(headers.size());
  for (const auto& header : headers) {
    uint32_t index = table_.getIndex(header);
    if (index > 0 && table_.inReferenceSet(index)) {
      toEncode.push_back(index);
    }
  }
  // what's in the reference set and not in the headers list - O(NlogN)
  std::sort(toEncode.begin(), toEncode.end());
  vector<uint32_t> toRemove;
  toRemove.reserve(refset.size());
  for (auto index : refset) {
    if (!std::binary_search(toEncode.begin(), toEncode.end(), index)) {
      toRemove.push_back(index);
    }
  }

  if (!toRemove.empty()) {
    // if we need to remove more than we keep in the refset, it's better to
    // empty the refset entirely
    if (refset.size() - toRemove.size() < toRemove.size()) {
      clearReferenceSet();
    } else {
      for (auto index : toRemove) {
        encodeAsIndex(dynamicToGlobalIndex(index));
        table_.removeReference(index);
      }
    }
  }
}

void HPACKEncoder::encodeAsLiteral(const HPACKHeader& header) {
  bool indexing = header.isIndexable();
  uint8_t prefix = indexing ?
    HPACK::HeaderEncoding::LITERAL_INCR_INDEXING :
    HPACK::HeaderEncoding::LITERAL_NO_INDEXING;
  // name
  uint32_t index = nameIndex(header.name);
  if (index) {
    buffer_.encodeInteger(index, prefix, 6);
  } else {
    buffer_.encodeInteger(0, prefix, 6);
    buffer_.encodeLiteral(header.name);
  }
  // value
  buffer_.encodeLiteral(header.value);
  // indexed ones need to get added to the header table
  if (indexing) {
    if (table_.add(header)) {
      table_.addReference(1);
    }
  }
}

void HPACKEncoder::encodeAsIndex(uint32_t index) {
  buffer_.encodeInteger(index, HPACK::HeaderEncoding::INDEXED, 7);
}

void HPACKEncoder::clearReferenceSet() {
  encodeAsIndex(0);
  table_.clearReferenceSet();
}

void HPACKEncoder::encodeHeader(const HPACKHeader& header) {
  uint32_t index = getIndex(header);
  if (index) {
    // firstly check if it's part of the static table
    if (isStatic(index)) {
      encodeAsIndex(index);
      // insert the static header in the dynamic header table
      // to take advantage of the delta compression
      if (table_.add(getStaticHeader(index))) {
        table_.addReference(1);
      }
    } else if (!table_.inReferenceSet(globalToDynamicIndex(index))) {
      table_.addReference(globalToDynamicIndex(index));
      encodeAsIndex(index);
    } else {
      // there's nothing to encode, but keep a record for it in case of eviction
      table_.addSkippedReference(globalToDynamicIndex(index));
    }
  } else {
    encodeAsLiteral(header);
  }
}

}
