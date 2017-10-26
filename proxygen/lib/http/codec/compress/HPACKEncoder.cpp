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

namespace {
// Some number close to typical MTU + room for "overhead"
const uint16_t kAutoFlushThreshold = 1400;
}

namespace proxygen {

bool HPACKEncoder::sEnableAutoFlush_{false};

HPACKEncoder::HPACKEncoder(bool huffman,
                           uint32_t tableSize,
                           bool emitSequenceNumbers,
                           bool useBaseIndex,
                           bool autoCommit) :
    // We only need the 'QCRAM' table if we are using sequent numbers
    HPACKContext(tableSize, emitSequenceNumbers, useBaseIndex),
    huffman_(huffman),
    buffer_(kBufferGrowth, huffman::huffTree(), huffman),
    emitSequenceNumbers_(emitSequenceNumbers),
    autoCommit_(autoCommit) {
  // Default the encoder indexing strategy; it can be updated later as well
  setHeaderIndexingStrategy(HeaderIndexingStrategy::getDefaultInstance());
}

unique_ptr<IOBuf> HPACKEncoder::encode(const vector<HPACKHeader>& headers,
                                       uint32_t headroom,
                                       bool* eviction) {
  if (!sEnableAutoFlush_) {
    packetFlushed();
  }
  eviction_ = false;
  if (headroom) {
    buffer_.addHeadroom(headroom);
  }
  if (emitSequenceNumbers_) {
    bytesInPacket_ += buffer_.appendSequenceNumber(nextSequenceNumber_);
  }
  if (useBaseIndex_) {
    auto baseIndex = table_.markBaseIndex();
    VLOG(10) << "Emitting base index=" << baseIndex;
    bytesInPacket_ += buffer_.encodeInteger(baseIndex, 0, 0);
  }
  if (pendingContextUpdate_) {
    bytesInPacket_ += buffer_.encodeInteger(
      table_.capacity(),
      HPACK::HeaderEncoding::TABLE_SIZE_UPDATE,
      5);
    pendingContextUpdate_ = false;
  }
  for (const auto& header : headers) {
    encodeHeader(header);
  }
  if (eviction) {
    *eviction = eviction_;
  }
  if (autoCommit_) {
    commitEpoch_ = nextSequenceNumber_;
  }
  nextSequenceNumber_++;
  return buffer_.release();
}

void HPACKEncoder::encodeAsLiteral(const HPACKHeader& header, bool indexing) {
  if (indexingStrat_) {
    indexing &= indexingStrat_->indexHeader(header);
  }
  if (header.bytes() > table_.capacity()) {
    // May want to investigate further whether or not this is wanted.
    // Flushing the table on a large header frees up some memory,
    // however, there will be no compression do to an empty table, and
    // the table will fill up again fairly quickly
    indexing = false;
  }
  uint8_t prefix = indexing ?
    HPACK::HeaderEncoding::LITERAL_INCR_INDEXING :
    HPACK::HeaderEncoding::LITERAL_NO_INDEXING;
  uint8_t len = indexing ? 6 : 4;
  // name
  uint32_t index = nameIndex(header.name, commitEpoch_, nextSequenceNumber_);
  if (index) {
    VLOG(10) << "encoding name index=" << index;
    bytesInPacket_ += buffer_.encodeInteger(index, prefix, len);
  } else {
    bytesInPacket_ += buffer_.encodeInteger(0, prefix, len);
    bytesInPacket_ += buffer_.encodeLiteral(header.name.get());
  }
  // value
  bytesInPacket_ += buffer_.encodeLiteral(header.value);
  // indexed ones need to get added to the header table
  if (indexing) {
    bool eviction;
    table_.add(header, nextSequenceNumber_, eviction);
    eviction_ |= eviction;
  }
}

void HPACKEncoder::encodeAsIndex(uint32_t index) {
  bytesInPacket_ += buffer_.encodeInteger(index, HPACK::HeaderEncoding::INDEXED,
                                          7);
}

void HPACKEncoder::encodeHeader(const HPACKHeader& header) {
  if (sEnableAutoFlush_ && bytesInPacket_ > kAutoFlushThreshold) {
    packetFlushed();
  }
  uint32_t index = getIndex(header, commitEpoch_, packetEpoch_);
  bool indexable = true;
  if (index == std::numeric_limits<uint32_t>::max()) {
    VLOG(5) << "Not indexing redundant header=" << header.name << " value=" <<
      header.value;
    index = 0;
    indexable = false;
  }
  if (index) {
    encodeAsIndex(index);
  } else {
    encodeAsLiteral(header, indexable);
  }
}

}
