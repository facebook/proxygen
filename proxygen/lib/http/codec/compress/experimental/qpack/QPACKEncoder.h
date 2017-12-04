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

#include <folly/io/IOBuf.h>
#include <list>
#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKContext.h>
#include <proxygen/lib/http/codec/compress/HPACKEncodeBuffer.h>
#include <proxygen/lib/http/codec/compress/HeaderIndexingStrategy.h>
#include <proxygen/lib/http/codec/compress/HeaderTable.h>
#include <vector>

namespace proxygen {

class QPACKEncoder : public QPACKContext {

 public:
  explicit QPACKEncoder(bool huffman, uint32_t tableSize=HPACK::kTableSize);

  /**
   * Size of a new IOBuf which is added to the chain
   *
   * jemalloc will round up to 4k - overhead
   */
  static const uint32_t kBufferGrowth = 4000;

  /**
   * Encode the given headers and return the buffer
   */

  using EncodeResult = std::pair<std::unique_ptr<folly::IOBuf>,
                                 std::unique_ptr<folly::IOBuf>>;

  EncodeResult encode(
    const std::vector<HPACKHeader>& headers,
    uint32_t headroom = 0);

  void deleteAck(const folly::IOBuf* ackBits);

  void setHeaderIndexingStrategy(const HeaderIndexingStrategy* indexingStrat) {
    indexingStrat_ = indexingStrat;
  }

 protected:
  void encodeAsIndex(uint32_t index);

 private:
  void encodeHeader(const HPACKHeader& header);

  void encodeAsLiteral(const HPACKHeader& header);

  void encodeDelete(uint32_t delIndex, uint32_t refcount);

  void encodeLiteral(HPACKEncodeBuffer& buffer,
                     const HPACKHeader& header,
                     uint8_t nameIndexPrefixLen);

  uint32_t encodeTableInsert(const HPACKHeader& header);

  const HeaderIndexingStrategy* indexingStrat_;

 protected:
  HPACKEncodeBuffer controlBuffer_;
  HPACKEncodeBuffer streamBuffer_;
  uint32_t minFree_{128};
};

}
