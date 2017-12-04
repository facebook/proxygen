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
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKConstants.h>

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
    controlBuffer_(kBufferGrowth, huffman::huffTree(), huffman),
    streamBuffer_(kBufferGrowth, huffman::huffTree(), huffman),
    minFree_(std::min(kMinFree, tableSize)) {
  // Default the encoder indexing strategy; it can be updated later as well
  setHeaderIndexingStrategy(HeaderIndexingStrategy::getDefaultInstance());
}

QPACKEncoder::EncodeResult
QPACKEncoder::encode(const vector<HPACKHeader>& headers,
                     uint32_t headroom) {
  if (headroom) {
    controlBuffer_.addHeadroom(headroom);
    streamBuffer_.addHeadroom(headroom);
  }
  for (const auto& header : headers) {
    encodeHeader(header);
  }
  return {controlBuffer_.release(), streamBuffer_.release()};
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
  bool isStaticIndex = isStatic(index);
  uint8_t prefix = QPACK::INDEX_REF.instruction;
  if (isStaticIndex) {
    prefix |= QPACK::STATIC_HEADER;
    index = globalToStaticIndex(index);
  } else {
    index = globalToDynamicIndex(index);
  }
  streamBuffer_.encodeInteger(index, prefix, QPACK::INDEX_REF.prefixLength);
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
  if (indexing) {
    encodeAsIndex(encodeTableInsert(header));
  } else {
    // not indexing, has to be iteral
    encodeLiteral(streamBuffer_, header, QPACK::LITERAL.prefixLength);
  }
}

uint32_t QPACKEncoder::encodeTableInsert(const HPACKHeader& header) {
  uint32_t newIndex = table_.nextAvailableIndex();
  controlBuffer_.encodeInteger(newIndex, QPACK::INSERT.instruction,
                               QPACK::INSERT.prefixLength);

  encodeLiteral(controlBuffer_, header, QPACK::NAME_REF.prefixLength);
  // indexed ones need to get added to the header table
  table_.add(header, newIndex);
  auto tIndex = table_.getIndexRef(header);
  CHECK_EQ(newIndex, tIndex);
  return dynamicToGlobalIndex(newIndex);
}

void QPACKEncoder::encodeLiteral(HPACKEncodeBuffer& buffer,
                                 const HPACKHeader& header,
                                 uint8_t nameIndexPrefixLen) {
  // name
  uint32_t nameIdx = nameIndex(header.name);
  if (nameIdx) {
    uint8_t prefix = 0;
    if (isStatic(nameIdx)) {
      prefix = static_cast<uint8_t>(1) << nameIndexPrefixLen;
      nameIdx = globalToStaticIndex(nameIdx);
    } else {
      nameIdx = globalToDynamicIndex(nameIdx);
    }
    buffer.encodeInteger(nameIdx, prefix, nameIndexPrefixLen);
  } else {
    buffer.encodeInteger(0, QPACK::NONE.instruction,
                         QPACK::NONE.prefixLength);
    buffer.encodeLiteral(header.name.get());
  }
  // value
  buffer.encodeLiteral(header.value);
}

void QPACKEncoder::encodeDelete(uint32_t delIndex, uint32_t refcount) {
  controlBuffer_.encodeInteger(refcount, QPACK::DELETE.instruction,
                               QPACK::DELETE.prefixLength);
  controlBuffer_.encodeInteger(delIndex, QPACK::NONE.instruction,
                               QPACK::NONE.prefixLength);
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
