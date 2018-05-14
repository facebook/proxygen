/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HPACKDecoder.h>

#include <proxygen/lib/http/codec/compress/HeaderCodec.h>

using folly::IOBuf;
using folly::io::Cursor;
using std::unique_ptr;
using proxygen::HPACK::DecodeError;

namespace proxygen {
HeaderDecodeError hpack2headerCodecError(HPACK::DecodeError err) {
  switch (err) {
    case HPACK::DecodeError::NONE:
      return HeaderDecodeError::NONE;
    case HPACK::DecodeError::INVALID_INDEX:
      return HeaderDecodeError::INVALID_INDEX;
    case HPACK::DecodeError::INVALID_HUFFMAN_CODE:
      return HeaderDecodeError::INVALID_HUFFMAN_CODE;
    case HPACK::DecodeError::INVALID_ENCODING:
      return HeaderDecodeError::INVALID_ENCODING;
    case HPACK::DecodeError::INTEGER_OVERFLOW:
      return HeaderDecodeError::INTEGER_OVERFLOW;
    case HPACK::DecodeError::INVALID_TABLE_SIZE:
      return HeaderDecodeError::INVALID_TABLE_SIZE;
    case HPACK::DecodeError::HEADERS_TOO_LARGE:
      return HeaderDecodeError::HEADERS_TOO_LARGE;
    case HPACK::DecodeError::BUFFER_UNDERFLOW:
      return HeaderDecodeError::BUFFER_UNDERFLOW;
    case HPACK::DecodeError::LITERAL_TOO_LARGE:
      return HeaderDecodeError::LITERAL_TOO_LARGE;
    case HPACK::DecodeError::TIMEOUT:
      return HeaderDecodeError::TIMEOUT;
    case HPACK::DecodeError::CANCELLED:
      return HeaderDecodeError::CANCELLED;
    case HPACK::DecodeError::INVALID_ACK:
      return HeaderDecodeError::INVALID_ACK;
  }
  return HeaderDecodeError::NONE;
}

unique_ptr<HPACKDecoder::headers_t> HPACKDecoder::decode(const IOBuf* buffer) {
  auto headers = std::make_unique<headers_t>();
  Cursor cursor(buffer);
  uint32_t totalBytes = buffer ? cursor.totalLength() : 0;
  decode(cursor, totalBytes, *headers);
  // release ownership of the set of headers
  return headers;
}

uint32_t HPACKDecoder::decode(Cursor& cursor,
                              uint32_t totalBytes,
                              headers_t& headers) {
  uint32_t emittedSize = 0;
  HPACKDecodeBuffer dbuf(cursor, totalBytes, maxUncompressed_);
  while (!hasError() && !dbuf.empty()) {
    emittedSize += decodeHeader(dbuf, &headers);
    if (emittedSize > maxUncompressed_) {
      LOG(ERROR) << "exceeded uncompressed size limit of "
                 << maxUncompressed_ << " bytes";
      err_ = DecodeError::HEADERS_TOO_LARGE;
      return dbuf.consumedBytes();
    }
  }
  return dbuf.consumedBytes();
}

void HPACKDecoder::decodeStreaming(
  Cursor& cursor,
  uint32_t totalBytes,
  HeaderCodec::StreamingCallback* streamingCb) {
  HPACKDecodeBuffer dbuf(cursor, totalBytes, maxUncompressed_);
  uint32_t emittedSize = 0;
  streamingCb_ = streamingCb;

  while (!hasError() && !dbuf.empty()) {
    emittedSize += decodeHeader(dbuf, nullptr);

    if (emittedSize > maxUncompressed_) {
      LOG(ERROR) << "exceeded uncompressed size limit of "
                 << maxUncompressed_ << " bytes";
      err_ = HPACK::DecodeError::HEADERS_TOO_LARGE;
      break;
    }
    emittedSize += 2;
  }

  completeDecode(dbuf.consumedBytes(), emittedSize);
}

uint32_t HPACKDecoder::decodeLiteralHeader(HPACKDecodeBuffer& dbuf,
                                           headers_t* emitted) {
  uint8_t byte = dbuf.peek();
  bool indexing = byte & HPACK::LITERAL_INC_INDEX.code;
  HPACKHeader header;
  uint8_t indexMask = 0x3F;  // 0011 1111
  uint8_t length = HPACK::LITERAL_INC_INDEX.prefixLength;
  if (!indexing) {
    // bool neverIndex = byte & HPACK::LITERAL_NEV_INDEX.code;
    // TODO: we need to emit this flag with the headers
    indexMask = 0x0F; // 0000 1111
    length = HPACK::LITERAL.prefixLength;
  }
  if (byte & indexMask) {
    uint32_t index;
    err_ = dbuf.decodeInteger(length, index);
    if (err_ != HPACK::DecodeError::NONE) {
      LOG(ERROR) << "Decode error decoding index err_=" << err_;
      return 0;
    }
    // validate the index
    if (!isValid(index)) {
      LOG(ERROR) << "received invalid index: " << index;
      err_ = HPACK::DecodeError::INVALID_INDEX;
      return 0;
    }
    header.name = getHeader(index).name;
  } else {
    // skip current byte
    dbuf.next();
    folly::fbstring headerName;
    err_ = dbuf.decodeLiteral(headerName);
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

uint32_t HPACKDecoder::decodeIndexedHeader(HPACKDecodeBuffer& dbuf,
                                           headers_t* emitted) {
  uint32_t index;
  err_ = dbuf.decodeInteger(HPACK::INDEX_REF.prefixLength, index);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Decode error decoding index err_=" << err_;
    return 0;
  }
  // validate the index
  if (index == 0 || !isValid(index)) {
    LOG(ERROR) << "received invalid index: " << index;
    err_ = HPACK::DecodeError::INVALID_INDEX;
    return 0;
  }
  uint32_t emittedSize = 0;

  auto& header = getHeader(index);
  return emit(header, emitted);
}

bool HPACKDecoder::isValid(uint32_t index) {
  if (isStatic(index)) {
    return getStaticTable().isValid(globalToStaticIndex(index));
  } else {
    return table_.isValid(globalToDynamicIndex(index));
  }
}

uint32_t HPACKDecoder::decodeHeader(HPACKDecodeBuffer& dbuf,
                                    headers_t* emitted) {
  uint8_t byte = dbuf.peek();
  if (byte & HPACK::INDEX_REF.code) {
    return decodeIndexedHeader(dbuf, emitted);
  } else if (byte & HPACK::LITERAL_INC_INDEX.code) {
    // else it's fine, fall through to decodeLiteralHeader
  } else if (byte & HPACK::TABLE_SIZE_UPDATE.code) {
    handleTableSizeUpdate(dbuf, table_);
    return 0;
  } // else LITERAL
  // LITERAL_NO_INDEXING or LITERAL_INCR_INDEXING
  return decodeLiteralHeader(dbuf, emitted);
}

}
