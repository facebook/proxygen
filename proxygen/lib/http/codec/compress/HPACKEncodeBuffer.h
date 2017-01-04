/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/Cursor.h>
#include <folly/io/IOBufQueue.h>
#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/Huffman.h>

namespace proxygen {

class HPACKEncodeBuffer {

 public:
  HPACKEncodeBuffer(
    uint32_t growthSize,
    const huffman::HuffTree& huffmanTree,
    bool huffmanEnabled);

  explicit HPACKEncodeBuffer(uint32_t growthSize);

  ~HPACKEncodeBuffer() {}

  /**
   * transfer ownership of the underlying IOBuf's
   */
  std::unique_ptr<folly::IOBuf> release() {
    return bufQueue_.move();
  }

  void clear() {
    bufQueue_.clear();
  }

  /**
   * Add headroom at the beginning of the IOBufQueue
   * Meant to be called before encoding anything.
   */
  void addHeadroom(uint32_t bytes);

  /**
   * Encode the integer value using variable-length layout and the given prefix
   * that spans nbit bits.
   * The prefix is given as 1-byte value (not need for shifting) used only for
   * the first byte. It starts from MSB.
   *
   * For example for integer=3, prefix=0x80, nbit=6:
   *
   * MSB           LSB
   * X X 0 0 0 0 1 1 (value)
   * 1 0 X X X X X X (prefix)
   * 1 0 0 0 0 0 1 1 (encoded value)
   *
   * @return how many bytes were used to encode the value
   */
  uint32_t encodeInteger(uint32_t value, uint8_t prefix, uint8_t nbit);

  /**
   * encodes a string, either header name or header value
   *
   * @return bytes used for encoding
   */
  uint32_t encodeLiteral(const std::string& literal);

  /**
   * encodes a string using huffman encoding
   */
  uint32_t encodeHuffman(const std::string& literal);

  /**
   * prints the content of an IOBuf in binary format. Useful for debugging.
   */
  std::string toBin();

 private:

  /**
   * append one byte at the end of buffer ensuring we always have enough space
   */
  void append(uint8_t byte);

  uint32_t growthSize_;
  folly::IOBufQueue bufQueue_;
  folly::io::QueueAppender buf_;
  const huffman::HuffTree& huffmanTree_;
  bool huffmanEnabled_;
};

}
