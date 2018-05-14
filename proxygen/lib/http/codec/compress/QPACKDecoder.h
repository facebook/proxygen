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

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/HPACKDecoderBase.h>
#include <proxygen/lib/http/codec/compress/HPACKDecodeBuffer.h>
#include <proxygen/lib/http/codec/compress/QPACKContext.h>
#include <folly/io/async/DestructorCheck.h>

namespace proxygen {

class QPACKDecoder : public HPACKDecoderBase,
                     public QPACKContext,
                     public folly::DestructorCheck {
 public:
  explicit QPACKDecoder(
    uint32_t tableSize=HPACK::kTableSize,
    uint32_t maxUncompressed=HeaderCodec::kMaxUncompressed)
      : HPACKDecoderBase(tableSize, maxUncompressed),
        QPACKContext(tableSize, false /* don't track references */) {}

  /**
   * given a Cursor and a total amount of bytes we can consume from it,
   * decode headers into the given vector.
   */
  uint32_t decode(folly::io::Cursor& cursor,
                  uint32_t totalBytes,
                  headers_t& headers);

  /**
   * given a Cursor and a total amount of bytes we can consume from it,
   * decode headers and invoke a callback.  If it takes a cursor, QPACKDecoder
   * does not take ownership of the decoded block, and cannot queue.
   */
  void decodeStreaming(folly::io::Cursor& cursor,
                       uint32_t totalBytes,
                       HeaderCodec::StreamingCallback* streamingCb);

  void decodeStreaming(std::unique_ptr<folly::IOBuf> block,
                       uint32_t totalBytes,
                       HeaderCodec::StreamingCallback* streamingCb);

  /**
   * given a compressed header block as an IOBuf chain, decode all the
   * headers and return them. This is just a convenience wrapper around
   * the API above.
   */
  std::unique_ptr<headers_t> decode(const folly::IOBuf* buffer);

  HPACK::DecodeError decodeControl(folly::io::Cursor& cursor,
                                   uint32_t totalBytes);

  uint64_t getHolBlockCount() const {
    return holBlockCount_;
  }

  uint64_t getQueuedBytes() const {
    return queuedBytes_;
  }

 private:
  bool isValid(bool isStatic, uint32_t index, bool aboveBase);

  uint32_t handleBaseIndex(HPACKDecodeBuffer& dbuf);

  void decodeStreamingImpl(uint32_t consumed,
                           HPACKDecodeBuffer& dbuf,
                           HeaderCodec::StreamingCallback* streamingCb);

  uint32_t decodeHeaderQ(HPACKDecodeBuffer& dbuf, headers_t* emitted);

  uint32_t decodeIndexedHeaderQ(HPACKDecodeBuffer& dbuf,
                                uint32_t prefixLength,
                                bool aboveBase,
                                headers_t* emitted);

  uint32_t decodeLiteralHeaderQ(HPACKDecodeBuffer& dbuf,
                                bool indexing,
                                bool nameIndexed,
                                uint8_t prefixLength,
                                bool aboveBase,
                                headers_t* emitted);

  void decodeControlHeader(HPACKDecodeBuffer& dbuf);

  void enqueueHeaderBlock(uint32_t largestReference,
                          uint32_t baseIndex,
                          uint32_t consumed,
                          std::unique_ptr<folly::IOBuf> block,
                          size_t length,
                          HeaderCodec::StreamingCallback* streamingCb);

  struct PendingBlock {
    PendingBlock(uint32_t bi, uint32_t l, uint32_t cons,
                 std::unique_ptr<folly::IOBuf> b,
                 HeaderCodec::StreamingCallback* c)
        : baseIndex(bi), length(l), consumed(cons), block(std::move(b)), cb(c)
      {}
    uint32_t baseIndex;
    uint32_t length;
    uint32_t consumed;
    std::unique_ptr<folly::IOBuf> block;
    HeaderCodec::StreamingCallback* cb;
  };

  // Returns true if this object was destroyed by its callback.  Callers
  // should check the result and immediately return.
  bool decodeBlock(const PendingBlock& pending);

  void drainQueue();

  uint32_t baseIndex_{0};
  uint32_t holBlockCount_{0};
  uint64_t queuedBytes_{0};
  std::multimap<uint32_t, PendingBlock> queue_;
};

}
