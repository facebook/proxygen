/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Conv.h>
#include <folly/FBString.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/Huffman.h>

namespace proxygen {

class HPACKDecodeBuffer {
 public:

  explicit HPACKDecodeBuffer(folly::io::Cursor& cursorVal,
                             uint32_t totalBytes,
                             uint32_t maxLiteralSize)
      : cursor_(cursorVal),
        totalBytes_(totalBytes),
        remainingBytes_(totalBytes),
        maxLiteralSize_(maxLiteralSize) {}

  ~HPACKDecodeBuffer() {}

  void reset(folly::io::Cursor& cursorVal) {
    reset(cursorVal, folly::to<uint32_t>(cursorVal.totalLength()));
  }

  void reset(folly::io::Cursor& cursorVal,
             uint32_t totalBytes) {
    cursor_ = cursorVal;
    totalBytes_ = totalBytes;
    remainingBytes_ = totalBytes;
  }

  uint32_t consumedBytes() const {
    return totalBytes_ - remainingBytes_;
  }

  const folly::io::Cursor& cursor() const {
    return cursor_;
  }

  /**
   * @returns true if there are no more bytes to decode. Calling this method
   * might move the cursor from the current IOBuf to the next one
   */
  bool empty();

  /**
   * extracts one byte from the buffer and advances the cursor
   */
  uint8_t next();

  /**
   * just peeks at the next available byte without moving the cursor
   */
  uint8_t peek();

  /**
   * decode an integer from the current position, given a nbit prefix.
   * Ignores 8 - nbit bits in the first byte of the buffer.
   */
  HPACK::DecodeError decodeInteger(uint8_t nbit, uint64_t& integer);

  /**
   * As above but with no prefix
   */
  HPACK::DecodeError decodeInteger(uint64_t& integer);

  /**
   * decode a literal starting from the current position
   */
  HPACK::DecodeError decodeLiteral(folly::fbstring& literal);

  HPACK::DecodeError decodeLiteral(uint8_t nbit, folly::fbstring& literal);

private:
  folly::io::Cursor& cursor_;
  uint32_t totalBytes_;
  uint32_t remainingBytes_;
  uint32_t maxLiteralSize_{std::numeric_limits<uint32_t>::max()};
};

}
