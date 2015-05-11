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

#include <stdint.h>
#include <iostream>

namespace proxygen {

namespace HPACK {

const uint32_t kTableSize = 4096;
const uint32_t kMaxLiteralSize = 96 * 1024;

const uint8_t NBIT_MASKS[9] = {
  0xFF,  // 11111111, not used
  0xFE,  // 11111110
  0xFC,  // 11111100
  0xF8,  // 11111000
  0xF0,  // 11110000
  0xE0,  // 11100000
  0xC0,  // 11000000
  0x80,  // 10000000
  0x00   // 00000000
};

enum HeaderEncoding : uint8_t {
  LITERAL_NO_INDEXING = 0x40,   // 2-bit
  LITERAL_INCR_INDEXING = 0x00, // 2-bit
  INDEXED = 0x80                // 1-bit
};

enum LiteralEncoding : uint8_t {
  PLAIN = 0x00,
  HUFFMAN = 0x80
};

enum MessageType : uint8_t {
  REQ = 0,
  RESP = 1
};

enum class DecodeError : uint8_t {
  NONE = 0,
  INVALID_INDEX = 1,
  INVALID_HUFFMAN_CODE = 2,
  INVALID_ENCODING = 3,
  INTEGER_OVERFLOW = 4,
  INVALID_TABLE_SIZE = 5,
  HEADERS_TOO_LARGE = 6,
  BUFFER_UNDERFLOW = 7,
  LITERAL_TOO_LARGE = 8,
};

std::ostream& operator<<(std::ostream& os, DecodeError err);


}

}
