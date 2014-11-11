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

#include <folly/io/IOBuf.h>
#include <list>
#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/HPACKContext.h>
#include <proxygen/lib/http/codec/compress/HPACKEncodeBuffer.h>
#include <proxygen/lib/http/codec/compress/HeaderTable.h>
#include <vector>

namespace proxygen {

class HPACKEncoder : public HPACKContext {

 public:
  HPACKEncoder(HPACK::MessageType msgType,
               bool huffman,
               uint32_t tableSize=HPACK::kTableSize) :
      HPACKContext(msgType, tableSize),
      huffman_(huffman),
      buffer_(kBufferGrowth, msgType, huffman) {
  }

  /**
   * Size of a new IOBuf which is added to the chain
   */
  static const uint32_t kBufferGrowth = 4096;

  /**
   * Encode the given headers and return the buffer
   */
  std::unique_ptr<folly::IOBuf> encode(const std::vector<HPACKHeader>& headers,
                                       uint32_t headroom = 0);

 private:
  void encodeHeader(const HPACKHeader& header);

  virtual void encodeAsLiteral(const HPACKHeader& header);

  void encodeAsIndex(uint32_t index);

  void addHeader(const HPACKHeader& header);

  void encodeDelta(const std::vector<HPACKHeader>& headers);

  /**
   * Figures out in advance if the header is going to evict headers
   * that are part of the reference set and it will encode them using
   * double indexing technique: first one will remove the index from the
   * refset and the second one will add it again to the refset, emitting it
   */
  void encodeEvictedReferences(const HPACKHeader& header);

  /**
   * Returns true if the given header will be added to the header table
   */
  bool willBeAdded(const HPACKHeader& header);

  void clearReferenceSet();

  bool huffman_;
 protected:
  HPACKEncodeBuffer buffer_;
};

}
