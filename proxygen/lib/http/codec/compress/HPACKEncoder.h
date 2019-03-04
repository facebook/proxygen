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

#include <folly/io/IOBuf.h>
#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/HPACKEncoderBase.h>
#include <vector>

namespace proxygen {

class HPACKEncoder : public HPACKEncoderBase, public HPACKContext {

 public:
  explicit HPACKEncoder(bool huffman,
                        uint32_t tableSize=HPACK::kTableSize)
      : HPACKEncoderBase(huffman)
      , HPACKContext(tableSize) {}

  /**
   * Encode the given headers.
   */

  std::unique_ptr<folly::IOBuf> encode(
    const std::vector<HPACKHeader>& headers,
    uint32_t headroom = 0);

  void setHeaderTableSize(uint32_t size) {
    HPACKEncoderBase::setHeaderTableSize(table_, size);
  }

 private:
  void encodeAsIndex(uint32_t index);

  void encodeHeader(const HPACKHeader& header);

  bool encodeAsLiteral(const HPACKHeader& header, bool indexing);

  void encodeLiteral(const HPACKHeader& header,
                     uint32_t nameIndex,
                     const HPACK::Instruction& instruction);
};

}
