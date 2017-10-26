/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKEncoder.h>

#include <algorithm>
#include <unordered_set>

using folly::IOBuf;
using std::list;
using std::unique_ptr;
using std::unordered_set;
using std::vector;

namespace {
const uint32_t kMinFree = 256;
}

namespace proxygen {

QPACKEncoder::QPACKEncoder(bool huffman,
                           uint32_t tableSize) :
    QPACKContext(tableSize),
    buffer_(kBufferGrowth, huffman::huffTree(), huffman),
    minFree_(std::min(kMinFree, tableSize)) {
  // Default the encoder indexing strategy; it can be updated later as well
  setHeaderIndexingStrategy(HeaderIndexingStrategy::getDefaultInstance());
}

unique_ptr<IOBuf> QPACKEncoder::encode(const vector<HPACKHeader>& headers,
                                       uint32_t headroom) {
  if (headroom) {
    buffer_.addHeadroom(headroom);
  }
  for (const auto& header : headers) {
    encodeHeader(header);
  }
  return buffer_.release();
}

void QPACKEncoder::encodeHeader(const HPACKHeader& header) {
  uint32_t index = getIndex(header);
  if (index) {
    encodeAsIndex(index);
  } else {
    encodeAsLiteral(header);
  }
}

void QPACKEncoder::encodeAsIndex(uint32_t index) {
  buffer_.encodeInteger(index, HPACK::HeaderEncoding::INDEXED, 7);
}

void QPACKEncoder::encodeAsLiteral(const HPACKHeader& header) {
  uint32_t newTableSize = header.bytes() + table_.bytes();
  bool tableHasRoom = (newTableSize <= table_.capacity());
  uint32_t lowMark = table_.capacity() - minFree_;
  if (tableHasRoom && newTableSize > lowMark && table_.size() > 0) {
    // figure out something to evict, encode eviction
    VLOG(4) << "Evicting";
    uint32_t freedBytes = 0;
    // Free everything new over lowMark, up to the entire table
    uint32_t toFree = std::min(newTableSize - lowMark, table_.size());
    while (freedBytes < toFree) {
      uint32_t delIndex;
      uint32_t refcount;
      uint32_t bytes;
      std::tie(delIndex, bytes) = table_.evictNext();
      CHECK_NE(delIndex, 0);
      freedBytes += bytes;
      refcount = table_.encoderRemove(delIndex).first;
      encodeDelete(dynamicToGlobalIndex(delIndex), refcount);
    }
  }

  bool indexing = !indexingStrat_ || indexingStrat_->indexHeader(header);
  if (indexing && !tableHasRoom) {
    VLOG(4) << "Encoding h=" << header << " as literal because table is too "
      "full";
    indexing = false;
  }
  uint8_t prefix = indexing ?
    HPACK::HeaderEncoding::LITERAL_INCR_INDEXING :
    HPACK::HeaderEncoding::LITERAL_NO_INDEXING;
  uint8_t len = indexing ? 6 : 4;

  uint32_t newIndex = 0;
  if (indexing) {
    newIndex = table_.nextAvailableIndex();
    buffer_.encodeInteger(dynamicToGlobalIndex(newIndex), prefix, len);
    prefix = 0;
    len = 8;
  }

  // name
  uint32_t nameIdx = nameIndex(header.name);
  if (nameIdx) {
    buffer_.encodeInteger(nameIdx, prefix, len);
  } else {
    buffer_.encodeInteger(0, prefix, len);
    buffer_.encodeLiteral(header.name.get());
  }
  // value
  buffer_.encodeLiteral(header.value);
  // indexed ones need to get added to the header table
  if (indexing) {
    table_.add(header, newIndex);
  }
}

void QPACKEncoder::encodeDelete(uint32_t delIndex, uint32_t refcount) {
  buffer_.encodeInteger(refcount, HPACK::TABLE_SIZE_UPDATE, 5);
  buffer_.encodeInteger(delIndex, 0x00, 8);
}

void QPACKEncoder::deleteAck(const folly::IOBuf* ackBits) {
  folly::io::Cursor c(ackBits);
  uint32_t index = 1;
  while (c.totalLength()) {
    auto byte = c.read<uint8_t>();
    uint8_t mask = 1;
    for (auto i = 0; i < 8; i++) {
      if (byte & mask) {
        table_.encoderRemoveAck(index);
      }
      mask <<= 1;
      index++;
    }
  }
}

}
