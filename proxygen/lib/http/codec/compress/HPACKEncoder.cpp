/*
 *  Copyright (c) 2017, Facebook, Inc.
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

using folly::IOBuf;
using std::list;
using std::unique_ptr;
using std::unordered_set;
using std::vector;

namespace proxygen {

HPACKEncoder::HPACKEncoder(bool huffman,
                           uint32_t tableSize) :
    HPACKContext(tableSize),
    huffman_(huffman),
    buffer_(kBufferGrowth, huffman::huffTree(), huffman) {
}

HPACKEncoder::HPACKEncoder(const huffman::HuffTree& huffmanTree,
                           bool huffman,
                           uint32_t tableSize) :
    // since we already have the huffman tree, msgType doesn't matter
    HPACKContext(tableSize),
    huffman_(huffman),
    buffer_(kBufferGrowth, huffmanTree, huffman) {
}

unique_ptr<IOBuf> HPACKEncoder::encode(const vector<HPACKHeader>& headers,
                                       uint32_t headroom) {
  if (headroom) {
    buffer_.addHeadroom(headroom);
    headroom = 0;
  }
  if (pendingContextUpdate_) {
    buffer_.encodeInteger(table_.capacity(),
                          HPACK::HeaderEncoding::TABLE_SIZE_UPDATE,
                          5);
    pendingContextUpdate_ = false;
  }
  for (const auto& header : headers) {
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
  uint8_t len = indexing ? 6 : 4;
  // name
  uint32_t index = nameIndex(header.name);
  if (index) {
    buffer_.encodeInteger(index, prefix, len);
  } else {
    buffer_.encodeInteger(0, prefix, len);
    buffer_.encodeLiteral(header.name);
  }
  // value
  buffer_.encodeLiteral(header.value);
  // indexed ones need to get added to the header table
  if (indexing) {
    table_.add(header);
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
    encodeAsIndex(index);
  } else {
    encodeAsLiteral(header);
  }
}

}
