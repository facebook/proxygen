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

using folly::IOBuf;
using folly::io::Cursor;
using std::unique_ptr;
using proxygen::HPACK::DecodeError;

namespace proxygen {

unique_ptr<QPACKDecoder::headers_t> QPACKDecoder::decode(const IOBuf* buffer) {
  auto headers = std::make_unique<headers_t>();
  Cursor cursor(buffer);
  uint32_t totalBytes = buffer ? cursor.totalLength() : 0;
  decode(cursor, totalBytes, *headers);
  // release ownership of the set of headers
  return headers;
}

uint32_t QPACKDecoder::decode(Cursor& cursor,
                              uint32_t totalBytes,
                              headers_t& headers) {
  uint32_t emittedSize = 0;
  HPACKDecodeBuffer dbuf(cursor, totalBytes, maxUncompressed_);
  uint32_t largestReference = handleBaseIndex(dbuf);
  CHECK_LE(largestReference, table_.getBaseIndex());
  while (!hasError() && !dbuf.empty()) {
    emittedSize += decodeHeaderQ(dbuf, &headers);
    if (emittedSize > maxUncompressed_) {
      LOG(ERROR) << "exceeded uncompressed size limit of "
                 << maxUncompressed_ << " bytes";
      err_ = DecodeError::HEADERS_TOO_LARGE;
      return dbuf.consumedBytes();
    }
  }
  return dbuf.consumedBytes();
}

// Blocking implementation - may queue
void QPACKDecoder::decodeStreaming(
  std::unique_ptr<folly::IOBuf> block,
  uint32_t totalBytes,
  HeaderCodec::StreamingCallback* streamingCb) {
  Cursor cursor(block.get());
  HPACKDecodeBuffer dbuf(cursor, totalBytes, maxUncompressed_);
  uint32_t largestReference = handleBaseIndex(dbuf);
  if (largestReference > table_.getBaseIndex()) {
    VLOG(5) << "largestReference=" << largestReference << " > baseIndex=" <<
      table_.getBaseIndex() << ", queuing";
    folly::IOBufQueue q;
    q.append(std::move(block));
    q.trimStart(dbuf.consumedBytes());
    enqueueHeaderBlock(largestReference, baseIndex_,
                       dbuf.consumedBytes(), q.move(),
                       totalBytes - dbuf.consumedBytes(), streamingCb);
  } else {
    decodeStreamingImpl(0, dbuf, streamingCb);
  }
}

uint32_t QPACKDecoder::handleBaseIndex(HPACKDecodeBuffer& dbuf) {
  uint32_t largestReference;
  err_ = dbuf.decodeInteger(largestReference);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Decode error decoding baseIndex err_=" << err_;
    return 0;
  }
  VLOG(5) << "Decoded largestReference=" << largestReference;
  uint32_t delta = 0;
  bool neg = dbuf.peek() & HPACK::Q_DELTA_BASE_NEG;
  err_ = dbuf.decodeInteger(HPACK::Q_DELTA_BASE.prefixLength, delta);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Decode error decoding depends_=" << err_;
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
  uint32_t consumed, HPACKDecodeBuffer& dbuf,
  HeaderCodec::StreamingCallback* streamingCb) {
  uint32_t emittedSize = 0;
  streamingCb_ = streamingCb;

  while (!hasError() && !dbuf.empty()) {
    emittedSize += decodeHeaderQ(dbuf, nullptr);
    if (emittedSize > maxUncompressed_) {
      LOG(ERROR) << "exceeded uncompressed size limit of "
                 << maxUncompressed_ << " bytes";
      err_ = HPACK::DecodeError::HEADERS_TOO_LARGE;
      break;
    }
    emittedSize += 2;
  }

  completeDecode(consumed + dbuf.consumedBytes(), emittedSize);
}

uint32_t QPACKDecoder::decodeHeaderQ(HPACKDecodeBuffer& dbuf,
                                     headers_t* emitted) {
  uint8_t byte = dbuf.peek();
  if (byte & HPACK::Q_INDEXED.code) {
    return decodeIndexedHeaderQ(
      dbuf, HPACK::Q_INDEXED.prefixLength, false, emitted);
  } else if ((byte & HPACK::Q_LITERAL.code) == HPACK::Q_LITERAL.code) {
    return decodeLiteralHeaderQ(
        dbuf, false, false, HPACK::Q_LITERAL.prefixLength, false, emitted);
  } else if ((byte & HPACK::Q_LITERAL_NAME_REF_POST.code) ==
             HPACK::Q_LITERAL_NAME_REF_POST.code) {
    return decodeLiteralHeaderQ(
        dbuf, false, true, HPACK::Q_LITERAL_NAME_REF_POST.prefixLength, true,
        emitted);
  } else if (byte & HPACK::Q_INDEXED_POST.code) {
    return decodeIndexedHeaderQ(
      dbuf, HPACK::Q_INDEXED_POST.prefixLength, true, emitted);
  } else { //  Q_LITERAL_NAME_REF
    return decodeLiteralHeaderQ(
        dbuf, false, true, HPACK::Q_LITERAL_NAME_REF.prefixLength, false,
        emitted);
  }
}

HPACK::DecodeError QPACKDecoder::decodeControl(
  Cursor& cursor,
  uint32_t totalBytes) {
  streamingCb_ = nullptr;
  HPACKDecodeBuffer dbuf(cursor, totalBytes, maxUncompressed_);
  VLOG(6) << "Decoding control block";
  baseIndex_ = 0;
  while (!hasError() && !dbuf.empty()) {
    decodeControlHeader(dbuf);
  }
  if (!hasError()) {
    drainQueue();
  }
  return err_;
}

void QPACKDecoder::decodeControlHeader(HPACKDecodeBuffer& dbuf) {
  uint8_t byte = dbuf.peek();
  if (byte & HPACK::Q_INSERT_NAME_REF.code) {
    decodeLiteralHeaderQ(
      dbuf, true, true, HPACK::Q_INSERT_NAME_REF.prefixLength, false, nullptr);
  } else if (byte & HPACK::Q_INSERT_NO_NAME_REF.code) {
    decodeLiteralHeaderQ(
      dbuf, true, false, HPACK::Q_INSERT_NO_NAME_REF.prefixLength, false,
      nullptr);
  } else if (byte & HPACK::Q_TABLE_SIZE_UPDATE.code) {
    handleTableSizeUpdate(dbuf, table_);
  } else { // must be Q_DUPLICATE=000
    headers_t emitted;
    decodeIndexedHeaderQ(
      dbuf, HPACK::Q_DUPLICATE.prefixLength, false, &emitted);
    if (!hasError()) {
      CHECK(!emitted.empty());
      CHECK(table_.add(emitted[0]));
    }
  }
}

uint32_t QPACKDecoder::decodeLiteralHeaderQ(HPACKDecodeBuffer& dbuf,
                                            bool indexing,
                                            bool nameIndexed,
                                            uint8_t prefixLength,
                                            bool aboveBase,
                                            headers_t* emitted) {
  HPACKHeader header;
  if (nameIndexed) {
    uint32_t nameIndex = 0;
    bool isStaticName = !aboveBase && (dbuf.peek() & (1 << prefixLength));
    err_ = dbuf.decodeInteger(prefixLength, nameIndex);
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
    header.name = getHeader(
      isStaticName, nameIndex, baseIndex_, aboveBase).name;
  } else {
    folly::fbstring headerName;
    err_ = dbuf.decodeLiteral(prefixLength, headerName);
    header.name = headerName;
    if (err_ != HPACK::DecodeError::NONE) {
      LOG(ERROR) << "Error decoding header name err_=" << err_;
      return 0;
    }
  }
  // value
  err_ = dbuf.decodeLiteral(header.value);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Error decoding header value name=" << header.name
               << " err_=" << err_;
    return 0;
  }

  uint32_t emittedSize = emit(header, emitted);

  if (indexing) {
    table_.add(header);
  }

  return emittedSize;
}

uint32_t QPACKDecoder::decodeIndexedHeaderQ(HPACKDecodeBuffer& dbuf,
                                            uint32_t prefixLength,
                                            bool aboveBase,
                                            headers_t* emitted) {
  uint32_t index;
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
  return emit(header, emitted);
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

void QPACKDecoder::enqueueHeaderBlock(
  uint32_t largestReference,
  uint32_t baseIndex,
  uint32_t consumed,
  std::unique_ptr<folly::IOBuf> block,
  size_t length,
  HeaderCodec::StreamingCallback* streamingCb) {
  // TDOO: this queue is currently unbounded and has no timeouts
  CHECK_GT(largestReference, table_.getBaseIndex());
  queue_.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(largestReference),
    std::forward_as_tuple(baseIndex, length, consumed, std::move(block),
                          streamingCb));
  holBlockCount_++;
  VLOG(5) << "queued block=" << largestReference << " len=" << length;
  queuedBytes_ += length;
}

bool QPACKDecoder::decodeBlock(const PendingBlock& pending) {
  if (pending.length > 0) {
    VLOG(5) << "decodeBlock len=" << pending.length;
    folly::io::Cursor cursor(pending.block.get());
    HPACKDecodeBuffer dbuf(cursor, pending.length, maxUncompressed_);
    DCHECK_LE(pending.length, queuedBytes_);
    queuedBytes_ -= pending.length;
    baseIndex_ = pending.baseIndex;
    folly::DestructorCheck::Safety safety(*this);
    decodeStreamingImpl(pending.consumed, dbuf, pending.cb);
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
    if (decodeBlock(it->second)) {
      return;
    }
    queue_.erase(it);
    it = queue_.begin();
  }
}

}
