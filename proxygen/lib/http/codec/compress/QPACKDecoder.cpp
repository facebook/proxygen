/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/QPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/HPACKEncodeBuffer.h>

using folly::IOBuf;
using folly::io::Cursor;
using std::unique_ptr;
using proxygen::HPACK::DecodeError;

namespace {
const uint32_t kGrowth = 100;
}

namespace proxygen {

// Blocking implementation - may queue
void QPACKDecoder::decodeStreaming(
    uint64_t streamID,
    std::unique_ptr<folly::IOBuf> block,
    uint32_t totalBytes,
    HPACK::StreamingCallback* streamingCb) {
  Cursor cursor(block.get());
  HPACKDecodeBuffer dbuf(cursor, totalBytes, maxUncompressed_);
  err_ = HPACK::DecodeError::NONE;
  uint32_t largestReference = handleBaseIndex(dbuf);
  if (largestReference > table_.getBaseIndex()) {
    VLOG(5) << "largestReference=" << largestReference << " > baseIndex=" <<
      table_.getBaseIndex() << ", queuing";
    if (queue_.size() >= maxBlocking_) {
      VLOG(2) << "QPACK queue is full size=" << queue_.size()
              << " maxBlocking_=" << maxBlocking_;
      err_ = HPACK::DecodeError::TOO_MANY_BLOCKING;
      completeDecode(HeaderCodec::Type::QPACK, streamingCb, 0, 0);
    } else {
      folly::IOBufQueue q;
      q.append(std::move(block));
      q.trimStart(dbuf.consumedBytes());
      enqueueHeaderBlock(streamID, largestReference, baseIndex_,
                         dbuf.consumedBytes(), q.move(),
                         totalBytes - dbuf.consumedBytes(), streamingCb);
    }
  } else {
    decodeStreamingImpl(largestReference, 0, dbuf, streamingCb);
  }
}

uint32_t QPACKDecoder::handleBaseIndex(HPACKDecodeBuffer& dbuf) {
  uint64_t largestReference;
  err_ = dbuf.decodeInteger(largestReference);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Decode error decoding largest reference err_=" << err_;
    return 0;
  }
  VLOG(5) << "Decoded largestReference=" << largestReference;
  uint64_t delta = 0;
  if (dbuf.empty()) {
    LOG(ERROR) << "Invalid prefix, no delta-base";
    err_ = HPACK::DecodeError::BUFFER_UNDERFLOW;
    return 0;
  }
  bool neg = dbuf.peek() & HPACK::Q_DELTA_BASE_NEG;
  err_ = dbuf.decodeInteger(HPACK::Q_DELTA_BASE.prefixLength, delta);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Decode error decoding delta base=" << err_;
    return 0;
  }
  if (neg) {
    if (delta > largestReference) {
      LOG(ERROR) << "Invalid delta=" << delta << " largestReference="
                 << largestReference;
      err_ = HPACK::DecodeError::INVALID_INDEX;
      return 0;
    }
    baseIndex_ = largestReference - delta;
  } else {
    baseIndex_ = largestReference + delta;
  }
  VLOG(5) << "Decoded baseIndex_=" << baseIndex_;
  return largestReference;
}

void QPACKDecoder::decodeStreamingImpl(
    uint32_t largestReference,
    uint32_t consumed, HPACKDecodeBuffer& dbuf,
    HPACK::StreamingCallback* streamingCb) {
  uint32_t emittedSize = 0;

  while (!hasError() && !dbuf.empty()) {
    emittedSize += decodeHeaderQ(dbuf, streamingCb);
    if (emittedSize > maxUncompressed_) {
      LOG(ERROR) << "exceeded uncompressed size limit of "
                 << maxUncompressed_ << " bytes";
      err_ = HPACK::DecodeError::HEADERS_TOO_LARGE;
      break;
    }
    emittedSize += 2;
  }

  if (!hasError()) {
    // This is a little premature, since the ack doesn't get generated here.
    // lastAcked_ is only read in encodeTableStateSync, so all completed header
    // blocks must be call encodeHeaderAck BEFORE calling encodeTableStateSync.
    lastAcked_ = std::max(lastAcked_, largestReference);
  }
  completeDecode(HeaderCodec::Type::QPACK, streamingCb,
                 consumed + dbuf.consumedBytes(), emittedSize);
}

uint32_t QPACKDecoder::decodeHeaderQ(
    HPACKDecodeBuffer& dbuf,
    HPACK::StreamingCallback* streamingCb) {
  uint8_t byte = dbuf.peek();
  if (byte & HPACK::Q_INDEXED.code) {
    return decodeIndexedHeaderQ(
        dbuf, HPACK::Q_INDEXED.prefixLength, false, streamingCb, nullptr);
  } else if (byte & HPACK::Q_LITERAL_NAME_REF.code) {
    return decodeLiteralHeaderQ(
        dbuf, false, true, HPACK::Q_LITERAL_NAME_REF.prefixLength, false,
        streamingCb);
  } else if (byte & HPACK::Q_LITERAL.code) {
    return decodeLiteralHeaderQ(
        dbuf, false, false, HPACK::Q_LITERAL.prefixLength, false, streamingCb);
  } else if (byte & HPACK::Q_INDEXED_POST.code) {
    return decodeIndexedHeaderQ(
        dbuf, HPACK::Q_INDEXED_POST.prefixLength, true, streamingCb, nullptr);
  } else { // Q_LITERAL_NAME_REF_POST
    return decodeLiteralHeaderQ(
        dbuf, false, true, HPACK::Q_LITERAL_NAME_REF_POST.prefixLength, true,
        streamingCb);
  }
}

HPACK::DecodeError QPACKDecoder::decodeEncoderStream(
    std::unique_ptr<folly::IOBuf> buf) {
  ingress_.append(std::move(buf));
  Cursor cursor(ingress_.front());
  HPACKDecodeBuffer dbuf(cursor, ingress_.chainLength(), maxUncompressed_);
  VLOG(6) << "Decoding control block";
  baseIndex_ = 0;
  err_ = HPACK::DecodeError::NONE;
  while (!hasError() && !dbuf.empty()) {
    decodeEncoderStreamInstruction(dbuf);
    if (err_ == HPACK::DecodeError::BUFFER_UNDERFLOW) {
      ingress_.trimStart(partial_.consumed);
      drainQueue();
      return HPACK::DecodeError::NONE;
    }
  }
  ingress_.trimStart(dbuf.consumedBytes());
  if (hasError()) {
    return err_;
  } else {
    drainQueue();
    return HPACK::DecodeError::NONE;
  }
}

void QPACKDecoder::decodeEncoderStreamInstruction(HPACKDecodeBuffer& dbuf) {
  uint8_t byte = dbuf.peek();
  partial_.consumed = dbuf.consumedBytes();
  if (partial_.state == Partial::VALUE ||
      byte & HPACK::Q_INSERT_NAME_REF.code) {
    // If partial state is VALUE, it might have been a NO_NAME_REF instruction,
    // but we've already parsed the name, so it doesn't matter
    decodeLiteralHeaderQ(
        dbuf, true, true, HPACK::Q_INSERT_NAME_REF.prefixLength, false,
        nullptr);
  } else if (byte & HPACK::Q_INSERT_NO_NAME_REF.code) {
    decodeLiteralHeaderQ(
        dbuf, true, false, HPACK::Q_INSERT_NO_NAME_REF.prefixLength, false,
        nullptr);
  } else if (byte & HPACK::Q_TABLE_SIZE_UPDATE.code) {
    handleTableSizeUpdate(dbuf, table_);
  } else { // must be Q_DUPLICATE=000
    headers_t emitted;
    decodeIndexedHeaderQ(
        dbuf, HPACK::Q_DUPLICATE.prefixLength, false, nullptr, &emitted);
    if (!hasError()) {
      CHECK(!emitted.empty());
      table_.add(std::move(emitted[0]));
    }
  }
}

uint32_t QPACKDecoder::decodeLiteralHeaderQ(
    HPACKDecodeBuffer& dbuf,
    bool indexing,
    bool nameIndexed,
    uint8_t prefixLength,
    bool aboveBase,
    HPACK::StreamingCallback* streamingCb) {
  bool allowPartial = (streamingCb == nullptr);
  Partial localPartial;
  Partial* partial = (allowPartial) ? &partial_ : &localPartial;
  if (partial->state == Partial::NAME) {
    if (nameIndexed) {
      uint64_t nameIndex = 0;
      bool isStaticName = !aboveBase && (dbuf.peek() & (1 << prefixLength));
      err_ = dbuf.decodeInteger(prefixLength, nameIndex);
      if (allowPartial && err_ == HPACK::DecodeError::BUFFER_UNDERFLOW) {
        return 0;
      }
      if (err_ != HPACK::DecodeError::NONE) {
        LOG(ERROR) << "Decode error decoding index err_=" << err_;
        return 0;
      }
      if (!isStaticName) {
        nameIndex++;
      }
      // validate the index
      if (!isValid(isStaticName, nameIndex, aboveBase)) {
        LOG(ERROR) << "received invalid index: " << nameIndex;
        err_ = HPACK::DecodeError::INVALID_INDEX;
        return 0;
      }
      partial->header.name = getHeader(
          isStaticName, nameIndex, baseIndex_, aboveBase).name;
    } else {
      folly::fbstring headerName;
      err_ = dbuf.decodeLiteral(prefixLength, headerName);
      if (allowPartial && err_ == HPACK::DecodeError::BUFFER_UNDERFLOW) {
        return 0;
      }
      if (err_ != HPACK::DecodeError::NONE) {
        LOG(ERROR) << "Error decoding header name err_=" << err_;
        return 0;
      }
      partial->header.name = headerName;
    }
    partial->state = Partial::VALUE;
    partial->consumed = dbuf.consumedBytes();
  }
  // value
  err_ = dbuf.decodeLiteral(partial->header.value);
  if (allowPartial && err_ == HPACK::DecodeError::BUFFER_UNDERFLOW) {
    return 0;
  }
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Error decoding header value name=" << partial->header.name
               << " err_=" << err_;
    return 0;
  }
  partial->state = Partial::NAME;

  uint32_t emittedSize = emit(partial->header, streamingCb, nullptr);

  if (indexing) {
    table_.add(std::move(partial->header));
  }

  return emittedSize;
}

uint32_t QPACKDecoder::decodeIndexedHeaderQ(
    HPACKDecodeBuffer& dbuf,
    uint32_t prefixLength,
    bool aboveBase,
    HPACK::StreamingCallback* streamingCb,
    headers_t* emitted) {
  uint64_t index;
  bool isStatic = !aboveBase && (dbuf.peek() & (1 << prefixLength));
  err_ = dbuf.decodeInteger(prefixLength, index);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Decode error decoding index err_=" << err_;
    return 0;
  }
  if (!isStatic) {
    index++;
  }
  // validate the index
  if (index == 0 || !isValid(isStatic, index, aboveBase)) {
    LOG(ERROR) << "received invalid index: " << index;
    err_ = HPACK::DecodeError::INVALID_INDEX;
    return 0;
  }

  auto& header = getHeader(isStatic, index, baseIndex_, aboveBase);
  return emit(header, streamingCb, emitted);
}

bool QPACKDecoder::isValid(bool isStatic, uint32_t index, bool aboveBase) {
  if (isStatic) {
    return getStaticTable().isValid(index);
  } else {
    uint32_t baseIndex = baseIndex_;
    if (aboveBase) {
      baseIndex = baseIndex + index;
      index = 1;
    }
    return table_.isValid(index, baseIndex);
  }
}

std::unique_ptr<folly::IOBuf> QPACKDecoder::encodeTableStateSync() {
  uint32_t toAck = table_.getBaseIndex() - lastAcked_;
  if (toAck > 0) {
    VLOG(6) << "encodeTableStateSync toAck=" << toAck;
    HPACKEncodeBuffer ackEncoder(kGrowth, false);
    ackEncoder.encodeInteger(toAck, HPACK::Q_TABLE_STATE_SYNC);
    lastAcked_ = table_.getBaseIndex();
    return ackEncoder.release();
  } else {
    return nullptr;
  }
}

std::unique_ptr<folly::IOBuf> QPACKDecoder::encodeHeaderAck(
    uint64_t streamId) const {
  HPACKEncodeBuffer ackEncoder(kGrowth, false);
  VLOG(6) << "encodeHeaderAck id=" << streamId;
  ackEncoder.encodeInteger(streamId, HPACK::Q_HEADER_ACK);
  return ackEncoder.release();
}

std::unique_ptr<folly::IOBuf> QPACKDecoder::encodeCancelStream(
    uint64_t streamId) {
  // Remove this stream from the queue
  auto it = queue_.begin();
  while (it != queue_.end()) {
    if (it->second.streamID == streamId) {
      it = queue_.erase(it);
    } else {
      it++;
    }
  }
  HPACKEncodeBuffer ackEncoder(kGrowth, false);
  ackEncoder.encodeInteger(streamId, HPACK::Q_CANCEL_STREAM);
  return ackEncoder.release();
}

void QPACKDecoder::enqueueHeaderBlock(
  uint64_t streamID,
  uint32_t largestReference,
  uint32_t baseIndex,
  uint32_t consumed,
  std::unique_ptr<folly::IOBuf> block,
  size_t length,
  HPACK::StreamingCallback* streamingCb) {
  // TDOO: this queue is currently unbounded and has no timeouts
  CHECK_GT(largestReference, table_.getBaseIndex());
  queue_.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(largestReference),
    std::forward_as_tuple(streamID, baseIndex, length, consumed,
                          std::move(block), streamingCb));
  holBlockCount_++;
  VLOG(5) << "queued block=" << largestReference << " len=" << length;
  queuedBytes_ += length;
}

bool QPACKDecoder::decodeBlock(uint32_t largestReference,
                               const PendingBlock& pending) {
  if (pending.length > 0) {
    VLOG(5) << "decodeBlock len=" << pending.length;
    folly::io::Cursor cursor(pending.block.get());
    HPACKDecodeBuffer dbuf(cursor, pending.length, maxUncompressed_);
    DCHECK_LE(pending.length, queuedBytes_);
    queuedBytes_ -= pending.length;
    baseIndex_ = pending.baseIndex;
    folly::DestructorCheck::Safety safety(*this);
    decodeStreamingImpl(largestReference, pending.consumed, dbuf, pending.cb);
    // The callback way destroy this, if so stop queue processing
    if (safety.destroyed()) {
      return true;
    }
  }
  return false;
}

void QPACKDecoder::drainQueue() {
  auto it = queue_.begin();
  while (!queue_.empty() && it->first <= table_.getBaseIndex() &&
         !hasError()) {
    if (decodeBlock(it->first, it->second)) {
      return;
    }
    queue_.erase(it);
    it = queue_.begin();
  }
}

}
