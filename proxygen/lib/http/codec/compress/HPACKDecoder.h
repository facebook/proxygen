/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <list>
#include <memory>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/HPACKContext.h>
#include <proxygen/lib/http/codec/compress/HPACKDecodeBuffer.h>
#include <proxygen/lib/http/codec/compress/HPACKHeader.h>
#include <vector>

namespace proxygen {

class HPACKDecoder : public HPACKContext {
 public:
  enum Error : uint8_t {
    NONE = 0,
    INVALID_INDEX = 1,
    INVALID_HUFFMAN_CODE = 2,
    INVALID_ENCODING = 3,
    BUFFER_OVERFLOW = 4,
    INVALID_TABLE_SIZE = 5,
    HEADERS_TOO_LARGE = 6,
  };

  explicit HPACKDecoder(
    HPACK::MessageType msgType,
    uint32_t tableSize=HPACK::kTableSize,
    uint32_t maxUncompressed=HeaderCodec::kMaxUncompressed)
      : HPACKContext(msgType, tableSize),
        maxTableSize_(tableSize),
        maxUncompressed_(maxUncompressed) {}

  typedef std::vector<HPACKHeader> headers_t;

  /**
   * given a Cursor and a total amount of bytes we can consume from it,
   * decode headers into the given vector.
   */
  uint32_t decode(folly::io::Cursor& cursor,
                  uint32_t totalBytes,
                  headers_t& headers);
  /**
   * given a compressed header block as an IOBuf chain, decode all the
   * headers and return them. This is just a convenience wrapper around
   * the API above.
   */
  std::unique_ptr<headers_t> decode(const folly::IOBuf* buffer);

  Error getError() const {
    return err_;
  }

  bool hasError() const {
    return err_ != Error::NONE;
  }

  void setHeaderTableMaxSize(uint32_t maxSize) {
    maxTableSize_ = maxSize;
  }

 protected:
  bool isValid(uint32_t index);

  virtual uint32_t emitRefset(headers_t& emitted);

  virtual const huffman::HuffTree& getHuffmanTree() const;

  uint32_t emit(const HPACKHeader& header, headers_t& emitted);

  virtual uint32_t decodeIndexedHeader(HPACKDecodeBuffer& dbuf,
                                       headers_t& emitted);

  virtual uint32_t decodeLiteralHeader(HPACKDecodeBuffer& dbuf,
                                       headers_t& emitted);

  uint32_t decodeHeader(HPACKDecodeBuffer& dbuf, headers_t& emitted);

  Error err_{Error::NONE};
  uint32_t maxTableSize_;
  uint32_t maxUncompressed_;
};

}
