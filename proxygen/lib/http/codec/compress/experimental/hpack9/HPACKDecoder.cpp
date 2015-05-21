/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/hpack9/HPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/experimental/hpack9/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/experimental/hpack9/Huffman.h>

using folly::io::Cursor;

namespace proxygen {

const huffman::HuffTree& HPACKDecoder09::getHuffmanTree() const {
  return huffman::huffTree09();
}

void HPACKDecoder09::handleTableSizeUpdate(HPACKDecodeBuffer& dbuf) {
  uint32_t arg = 0;
  err_ = dbuf.decodeInteger(5, arg);
  if (err_ != HPACK::DecodeError::NONE) {
    LOG(ERROR) << "Decode error decoding maxSize err_=" << err_;
    return;
  }

  if (arg > maxTableSize_) {
    LOG(ERROR) << "Tried to increase size of the header table";
    err_ = HPACK::DecodeError::INVALID_TABLE_SIZE;
    return;
  }
  table_.setCapacity(arg);
}

uint32_t HPACKDecoder09::decodeLiteralHeader(HPACKDecodeBuffer& dbuf,
                                             headers_t* emitted) {
  uint8_t byte = dbuf.peek();
  bool indexing = byte & HPACK09::HeaderEncoding::LITERAL_INCR_INDEXING;
  HPACKHeader header;
  uint8_t indexMask = 0x3F;  // 0011 1111
  uint8_t length = 6;
  if (!indexing) {
    bool tableSizeUpdate = byte & HPACK09::HeaderEncoding::TABLE_SIZE_UPDATE;
    if (tableSizeUpdate) {
      handleTableSizeUpdate(dbuf);
      return 0;
    } else {
      bool neverIndex = byte & HPACK09::HeaderEncoding::LITERAL_NEVER_INDEXING;
      // TODO: we need to emit this flag with the headers
    }
    indexMask = 0x0F; // 0000 1111
    length = 4;
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
    err_ = dbuf.decodeLiteral(header.name);
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

uint32_t HPACKDecoder09::decodeIndexedHeader(HPACKDecodeBuffer& dbuf,
                                             headers_t* emitted) {
  uint32_t index;
  err_ = dbuf.decodeInteger(7, index);
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
  // a static index cannot be part of the reference set
  if (isStatic(index)) {
    auto& header = getStaticHeader(index);
    emittedSize = emit(header, emitted);
  } else {
    auto& header = getDynamicHeader(index);
    emittedSize = emit(header, emitted);
  }
  return emittedSize;
}

}
