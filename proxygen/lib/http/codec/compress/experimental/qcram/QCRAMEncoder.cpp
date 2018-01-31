/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMEncoder.h>

#include <proxygen/lib/http/codec/compress/experimental/simulator/QCRAMNewScheme.h>

#include <algorithm>
#include <unordered_set>

using folly::IOBuf;
using std::list;
using std::unique_ptr;
using std::unordered_set;
using std::vector;

namespace {
const uint32_t kMinFree = 256;
const uint16_t kAutoFlushThreshold = 1400;
}

namespace proxygen {

QCRAMEncoder::QCRAMEncoder(bool huffman,
                           uint32_t tableSize,
                           bool emitSequenceNumbers,
                           bool useBaseIndex,
                           bool autoCommit) :
    // We only need the 'QCRAM' table if we are using sequent numbers
    QCRAMContext(tableSize, emitSequenceNumbers, useBaseIndex),
    huffman_(huffman),
    buffer_(kBufferGrowth, huffman::huffTree(), huffman),
    prefix_(kBufferGrowth, huffman::huffTree(), huffman),
    emitSequenceNumbers_(emitSequenceNumbers),
    autoCommit_(autoCommit) {
  // Default the encoder indexing strategy; it can be updated later as well
  setHeaderIndexingStrategy(HeaderIndexingStrategy::getDefaultInstance());
}

QCRAMEncoder::EncodeResult QCRAMEncoder::encode(bool newPacket,
                                  const vector<QCRAMHeader>& headers,
                                  uint32_t headroom) {
  EncodeResult result;
  if (newPacket) {
    packetFlushed();
    bytesInPacket_ = 0;
  }
  if (compress::QCRAMNewScheme::sEnableUpdatesOnControlStream_) {
    controlBlock_ = true;
    // For Control Block, use depends to enforce total order.
    depends_ = table_.writeBaseIndex();
    if (headroom) {
      VLOG(1) << "add headroom " << headroom;
      buffer_.addHeadroom(headroom);
    }
    // TODO(ckrasic) - these are not actually needed, since a real
    // control stream is totally ordered.  Leaving them here for now
    // to focus on getting initial data on -04 peformance.
    if (emitSequenceNumbers_) {
      VLOG(1) << " Encode emit seqn " << nextSequenceNumber_;
      bytesInPacket_ += buffer_.appendSequenceNumber(nextSequenceNumber_);
    }
    if (useBaseIndex_) {
      auto baseIndex = table_.markBaseIndex();
      auto bytes = buffer_.encodeInteger(baseIndex, 0, 8);
      VLOG(1) << "Emitting base index=" << baseIndex << " bytes=" << bytes;
      bytesInPacket_ += bytes;
    }
    auto controlPrev = bytesInPacket_;
    // TODO(continue here...)
    for (const auto& header : headers) {
      encodeHeader(header);
    }
    if (controlPrev < bytesInPacket_) {
      VLOG(1) << "Encoder emit seqn (control) " << nextSequenceNumber_ << "  depends on " << depends_;
      prefix_.encodeInteger(depends_, 0, 8);
      result.controlBuffer = prefix_.release();
      result.controlBuffer->appendChain(buffer_.release());
      nextSequenceNumber_++;
    } else {
      // There were not table updates.
      prefix_.clear();
      buffer_.clear();
    }
  }
  controlBlock_ = false;
  depends_ = kMaxIndex;
  if (headroom) {
    VLOG(1) << "add headroom " << headroom;
    buffer_.addHeadroom(headroom);
  }
  if (emitSequenceNumbers_) {
    VLOG(1) << " Encode emit seqn " << nextSequenceNumber_;
    bytesInPacket_ += buffer_.appendSequenceNumber(nextSequenceNumber_);
  }
  if (useBaseIndex_) {
    auto baseIndex = table_.markBaseIndex();
    auto bytes = buffer_.encodeInteger(baseIndex, 0, 8);
    VLOG(1) << "Emitting base index=" << baseIndex << " bytes=" << bytes;
    bytesInPacket_ += bytes;
    baseIndexOverhead_ += bytes;
    VLOG(1) << "cumulative base index overhead " << baseIndexOverhead_;
  }
  if (pendingContextUpdate_) {
    bytesInPacket_ += buffer_.encodeInteger(
      table_.capacity(),
      HPACK::HeaderEncoding::TABLE_SIZE_UPDATE,
      5);
    pendingContextUpdate_ = false;
  }
  auto streamPrev = bytesInPacket_;
  for (const auto& header : headers) {
    encodeHeader(header);
  }
  if (streamPrev == bytesInPacket_) {
    VLOG(1) << "streamBuffer empty!";
    DCHECK(headerRefs_.empty());
    prefix_.clear();
    buffer_.clear();
    return std::move(result);
  }
  result.streamDepends = depends_;
  outstandingRefs_.emplace(nextSequenceNumber_, std::move(headerRefs_));
  if (depends_ < kMaxIndex) {
    VLOG(1) << "Encoder seqn " << nextSequenceNumber_ << "  depends on " << depends_;
    prefix_.encodeInteger(depends_, 0, 8);
    auto prefixBuf = prefix_.release();
    prefixBuf->appendChain(buffer_.release());
    nextSequenceNumber_++;
    result.streamBuffer = std::move(prefixBuf);
  } else {
    VLOG(1) << "Encoder seqn " << nextSequenceNumber_ << " no depends";
    result.streamBuffer = buffer_.release();
  }
  nextSequenceNumber_++;
  return std::move(result);
}

void QCRAMEncoder::encodeAsLiteral(const HPACKHeader& header, bool indexing) {
  // indexed ones need to get added to the header table
  uint32_t index = nameIndex(header.name, allowVulnerable_, commitEpoch_, packetEpoch_);
  if (indexing) {
    if (!table_.add(header, nextSequenceNumber_, packetEpoch_)) {
      VLOG(1) << "Not indexing because table add failed for header " << header;
      indexing = false;
    }
  }

  if (compress::QCRAMNewScheme::sEnableUpdatesOnControlStream_ &&
      controlBlock_ &&
      !indexing) {
    /* Do not do LITERAL_NO_INDEXING on control steam */
    return;
  }

  uint8_t prefix = indexing ?
    HPACK::HeaderEncoding::LITERAL_INCR_INDEXING :
    HPACK::HeaderEncoding::LITERAL_NO_INDEXING;
  uint8_t len = indexing ? 6 : 4;
  // name
  size_t before = bytesInPacket_;
  if (index && index != kMaxIndex) {
    VLOG(5) << "encoding name index=" << index;
    if (isVulnerable(index, commitEpoch_, packetEpoch_)) {
      uint32_t absolute = relativeToAbsoluteIndex(index);
      VLOG(1) << "using vulnerable name index " << index << " absolute " << absolute;
      if (depends_ == kMaxIndex) {
        depends_ = absolute;
      } else {
        depends_ = std::max(depends_, absolute);
      }
    }
    // Encode name as index
    addRef(index);
    bytesInPacket_ += buffer_.encodeInteger(index, prefix, len);
  } else {
    // Encode name as literal
    bytesInPacket_ += buffer_.encodeInteger(0, prefix, len);
    bytesInPacket_ += buffer_.encodeLiteral(header.name.get());
  }
  if (index == kMaxIndex) {
    VLOG(1) << "name found, but vulnerable, encoding as literal: " << header.name;
    literalOverhead_ += (bytesInPacket_ - before - 1); // 1 is approx.
  }
  // value
  bytesInPacket_ += buffer_.encodeLiteral(header.value);
}

void QCRAMEncoder::encodeAsIndex(uint32_t index) {
  auto bytes = buffer_.encodeInteger(index, HPACK::HeaderEncoding::INDEXED,
                                          7);
  VLOG(10) << "Encode as index " << index << " bytes " << bytes;
  addRef(index);
  bytesInPacket_ += bytes;
}

void QCRAMEncoder::encodeHeader(const HPACKHeader& header) {
  if (bytesInPacket_ > kAutoFlushThreshold) {
    VLOG(1) << "header seqn " << nextSequenceNumber_ << " spans packet boundary.";
    packetFlushed();
    bytesInPacket_ -= kAutoFlushThreshold;
  }

  // First determine whether the header is defined as indexable using the
  // set strategy if applicable, else assume it is indexable
  bool indexable = !indexingStrat_ || indexingStrat_->indexHeader(header);

  // If the header was not defined as indexable, its a reasonable assumption
  // that it does not appear in either the static or dynamic table and should
  // not be searched.  The only time this is not true is if the header indexing
  // strat specified an exact header/value pair that is in the static header
  // table although semantically the header indexing strategy should indeed act
  // as an override so we assume this is desired if such a case occurs
  uint32_t index = 0;
  if (indexable) {
    index = getIndex(header, allowVulnerable_, commitEpoch_, packetEpoch_);
    // So for QCRAM <=-01, max() signals it is in the table and but not
    // acked.  In that case do not enter it into the table again (and
    // again and again...) as it could blow up the table.  QCRAM >= 2
    // mandates string deduplication, so we need to add that.
    // Presumably this would blow up but much more slowly, so insertion
    // is still a win.
    if (index == kMaxIndex) {
      VLOG(5) << "Found header, but vulnerable, encoding value as literal: " << header;
      indexable = false;
    }
  }

  // Finally encode the header as determined above
  if (index && index != kMaxIndex) {
    // index
    if (compress::QCRAMNewScheme::sEnableUpdatesOnControlStream_ &&
        controlBlock_) {
      // Do not do indexed on control stream.
      return;
    }
    if (isVulnerable(index, commitEpoch_, packetEpoch_)) {
      uint32_t absolute = relativeToAbsoluteIndex(index);
      VLOG(1) << "using vulnerable header index " << index << " absolute " << absolute;
      if (depends_ == kMaxIndex) {
        depends_ = absolute;
      } else {
        depends_ = std::max(depends_, absolute);
      }
    }
    VLOG(2) << "encode indexed at index " << index << " header: " << header;
    encodeAsIndex(index);
    return;
  }
  // literal
  VLOG(2) << "emit literal " << (indexable ? "indexable" : "non-indexable") << " header: " << header;
  uint32_t prevLiteralOverhead =  literalOverhead_;
  uint32_t prevBytesInPacket = bytesInPacket_;
  encodeAsLiteral(header, indexable);
  if (index == kMaxIndex) {
    // approximate how many bytes literals due to vulnerable headers cost.
    literalOverhead_ += (bytesInPacket_ - prevBytesInPacket - (literalOverhead_ - prevLiteralOverhead) - 2);
    VLOG(1) << "cumulative literal overhead is " << literalOverhead_;
  }
}

void QCRAMEncoder::recvAck(uint16_t seqn) {
  if (outstandingRefs_.find(seqn) == outstandingRefs_.end()) {
    LOG(ERROR) << "missing outstanding refs for seqn " << seqn;
    return;
  }
  auto& refs = outstandingRefs_[seqn];
  for (auto ref : refs) {
    table_.subRef(ref);
  }
  outstandingRefs_.erase(seqn);
}


}
