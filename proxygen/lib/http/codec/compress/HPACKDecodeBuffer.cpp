/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HPACKDecodeBuffer.h>

#include <limits>
#include <memory>
#include <proxygen/lib/http/codec/compress/Huffman.h>

using folly::IOBuf;
using std::unique_ptr;

namespace proxygen {

bool HPACKDecodeBuffer::empty() {
  return remainingBytes_ == 0;
}

uint8_t HPACKDecodeBuffer::next() {
  CHECK(remainingBytes_ > 0);
  // in case we are the end of an IOBuf, peek() will move to the next one
  uint8_t byte = peek();
  cursor_.skip(1);
  remainingBytes_--;

  return byte;
}

uint8_t HPACKDecodeBuffer::peek() {
  CHECK(remainingBytes_ > 0);
  if (cursor_.length() == 0) {
    cursor_.peek();
  }
  return *cursor_.data();
}

bool HPACKDecodeBuffer::decodeLiteral(std::string& literal) {
  literal.clear();
  if (remainingBytes_ == 0) {
    return false;
  }
  auto byte = peek();
  bool huffman = byte & HPACK::LiteralEncoding::HUFFMAN;
  // extract the size
  uint32_t size;
  if (!decodeInteger(7, size)) {
    return false;
  }
  if (size > remainingBytes_ || size > HPACK::kMaxLiteralSize) {
    return false;
  }
  const uint8_t* data;
  unique_ptr<IOBuf> tmpbuf;
  // handle the case where the buffer spans multiple buffers
  if (cursor_.length() >= size) {
    data = cursor_.data();
    cursor_.skip(size);
  } else {
    // temporary buffer to pull the chunks together
    tmpbuf = IOBuf::create(size);
    // pull() will move the cursor
    cursor_.pull(tmpbuf->writableData(), size);
    data = tmpbuf->data();
  }
  if (huffman) {
    huffmanTree_.decode(data, size, literal);
  } else {
    literal.append((const char *)data, size);
  }
  remainingBytes_ -= size;
  return true;
}

bool HPACKDecodeBuffer::decodeInteger(uint8_t nbit, uint32_t& integer) {
  if (remainingBytes_ == 0) {
    return false;
  }
  uint8_t byte = next();
  uint8_t mask = ~HPACK::NBIT_MASKS[nbit] & 0xFF;
  // remove the first (8 - nbit) bits
  byte = byte & mask;
  integer = byte;
  if (byte != mask) {
    // the value fit in one byte
    return true;
  }
  uint32_t f = 1;
  uint32_t fexp = 0;
  do {
    if (remainingBytes_ == 0) {
      return false;
    }
    byte = next();
    if (fexp > 32) {
      // overflow in factorizer, f > 2^32
      return false;
    }
    uint32_t add = (byte & 127) * f;
    if (std::numeric_limits<uint32_t>::max() - integer < add) {
      // overflow detected
      return false;
    }
    integer += add;
    f = f << 7;
    fexp += 7;
  } while (byte & 128);
  return true;
}

}
