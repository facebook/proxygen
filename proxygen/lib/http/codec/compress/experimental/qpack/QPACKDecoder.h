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
#include <folly/io/IOBuf.h>
#include <list>
#include <memory>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKContext.h>
#include <proxygen/lib/http/codec/compress/HPACKDecodeBuffer.h>
#include <proxygen/lib/http/codec/compress/HPACKHeader.h>

namespace proxygen {

class QPACKDecoder : public QPACKContext {
 public:
  class Callback {
   public:
    virtual ~Callback() {}
    virtual void ack(uint32_t ackIndex) = 0;
    virtual void onError() = 0;
  };

  explicit QPACKDecoder(
    Callback& callback,
    uint32_t tableSize=HPACK::kTableSize,
    uint32_t maxUncompressed=HeaderCodec::kMaxUncompressed)
      : QPACKContext(tableSize),
        callback_(callback),
        maxUncompressed_(maxUncompressed) {}
  ~QPACKDecoder();

  void decodeControlStream(folly::io::Cursor& cursor,
                           uint32_t totalBytes);
  /**
   * given a Cursor and a total amount of bytes we can consume from it,
   * decode headers and invoke a callback.
   */
  bool decodeStreaming(folly::io::Cursor& cursor,
                       uint32_t totalBytes,
                       HeaderCodec::StreamingCallback* streamingCb);

  void setMaxUncompressed(uint32_t maxUncompressed) {
    maxUncompressed_ = maxUncompressed;
  }

  uint32_t getQueuedBytes() const {
    return queuedBytes_;
  }

 protected:
  struct DecodeRequest {
    bool allSubmitted{false};
    uint32_t pending{0};
    HeaderCodec::StreamingCallback* cb{nullptr};
    HTTPHeaderSize decodedSize;
    uint32_t consumedBytes{0};
    HPACK::DecodeError err{HPACK::DecodeError::NONE};

    DecodeRequest(HeaderCodec::StreamingCallback* inCb, uint32_t compressedSize)
        : cb(inCb) {
      decodedSize.compressed = compressedSize;
    }

    bool hasError() const {
      return err != HPACK::DecodeError::NONE;
    }
  };
  using DecodeRequestHandle = std::list<DecodeRequest>::iterator;

  bool isValid(uint32_t index);

  const huffman::HuffTree& getHuffmanTree() const;

  void emit(DecodeRequestHandle dreq, const HPACKHeader& header);

  void decodeIndexedHeader(HPACKDecodeBuffer& dbuf, DecodeRequestHandle dreq);

  void decodeLiteralHeader(HPACKDecodeBuffer& dbuf, DecodeRequestHandle dreq);

  void decodeDelete(HPACKDecodeBuffer& dbuf, DecodeRequestHandle dreq);

  void decodeHeader(HPACKDecodeBuffer& dbuf, DecodeRequestHandle dreq);

  void handleTableSizeUpdate(HPACKDecodeBuffer& dbuf, DecodeRequestHandle dreq);

  bool checkComplete(DecodeRequestHandle dreq);

  void decodeLiteral(HPACKDecodeBuffer& buffer,
                     DecodeRequestHandle dreq,
                     uint8_t nameIndexPrefixLen,
                     uint32_t newIndex);

  void decodeHeaderControl(HPACKDecodeBuffer& dbuf,
                           DecodeRequestHandle dreq);

  Callback& callback_;
  uint32_t maxUncompressed_;
  HeaderCodec::StreamingCallback* streamingCb_{nullptr};
  std::list<DecodeRequest> decodeRequests_;
  uint32_t queuedBytes_{0};
  uint32_t pendingDecodeBytes_{0};
};

}
