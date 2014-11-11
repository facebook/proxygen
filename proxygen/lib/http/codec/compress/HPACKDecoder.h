/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/compress/HPACKContext.h>
#include <proxygen/lib/http/codec/compress/HPACKDecodeBuffer.h>
#include <proxygen/lib/http/codec/compress/HPACKHeader.h>

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <list>
#include <memory>
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
  };

  explicit HPACKDecoder(HPACK::MessageType msgType,
                        uint32_t tableSize=HPACK::kTableSize)
      : HPACKContext(msgType, tableSize) {}

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

 private:
  bool isValid(uint32_t index);

  void emitRefset(headers_t& emitted);

  void emit(const HPACKHeader& header, headers_t& emitted);

  void decodeIndexedHeader(HPACKDecodeBuffer& dbuf, headers_t& emitted);

  void decodeLiteralHeader(HPACKDecodeBuffer& dbuf, headers_t& emitted);

  void decodeHeader(HPACKDecodeBuffer& dbuf, headers_t& emitted);

  Error err_{Error::NONE};
};

}
