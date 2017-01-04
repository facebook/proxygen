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

#include <memory>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/codec/compress/Header.h>
#include <proxygen/lib/http/codec/compress/HeaderPiece.h>
#include <proxygen/lib/utils/Result.h>
#include <vector>

namespace folly {
class IOBuf;
}

namespace folly { namespace io {
class Cursor;
}}

namespace proxygen {

enum class HeaderDecodeError : uint8_t {
  NONE = 0,
  BAD_ENCODING = 1,
  HEADERS_TOO_LARGE = 2,
  INFLATE_DICTIONARY = 3,
  EMPTY_HEADER_NAME = 4,
  EMPTY_HEADER_VALUE = 5,
  INVALID_HEADER_VALUE = 6,
};

struct HeaderDecodeResult {
  compress::HeaderPieceList& headers;
  uint32_t bytesConsumed;
};

class HeaderCodec {
 public:
  const static uint32_t kMaxUncompressed = 128 * 1024;

  enum class Type : uint8_t {
    GZIP = 0,
    HPACK = 1,
  };

  class Stats {
   public:
    Stats() {}
    virtual ~Stats() {}

    virtual void recordEncode(Type type, HTTPHeaderSize& size) = 0;
    virtual void recordDecode(Type type, HTTPHeaderSize& size) = 0;
    virtual void recordDecodeError(Type type) = 0;
    virtual void recordDecodeTooLarge(Type type) = 0;
  };

  class StreamingCallback {
   public:
    virtual ~StreamingCallback() {}

    virtual void onHeader(const std::string& name,
                          const std::string& value) = 0;
    virtual void onHeadersComplete() = 0;
    virtual void onDecodeError(HeaderDecodeError decodeError) = 0;
  };

  HeaderCodec() {}
  virtual ~HeaderCodec() {}

  /**
   * Encode the given headers and return an IOBuf chain.
   *
   * The list of headers might be mutated during the encode, like order
   * of the elements might change.
   */
  virtual std::unique_ptr<folly::IOBuf> encode(
    std::vector<compress::Header>& headers) noexcept = 0;

  /**
   * Decode headers given a Cursor and an amount of bytes to consume.
   *
   * @return Either the error that occurred while parsing the headers or
   * the decoded header list. A header decode error should be considered
   * fatal and no more bytes may be parsed from the cursor.
   */
  virtual Result<HeaderDecodeResult, HeaderDecodeError>
  decode(folly::io::Cursor& cursor, uint32_t length) noexcept = 0;

  /**
   * Decode headers given a Cursor and an amount of bytes to consume.
   */
  virtual void decodeStreaming(folly::io::Cursor& cursor, uint32_t length,
      StreamingCallback* streamingCb) noexcept = 0;

  /**
   * compressed and uncompressed size of the last encode
   */
  const HTTPHeaderSize& getEncodedSize() {
    return encodedSize_;
  }

  /**
   * same as above, but for decode
   */
  const HTTPHeaderSize& getDecodedSize() {
    return decodedSize_;
  }

  /**
   * amount of space to reserve as a headroom in the encode buffer
   */
  void setEncodeHeadroom(uint32_t headroom) {
    encodeHeadroom_ = headroom;
  }

  void setMaxUncompressed(uint32_t maxUncompressed) {
    maxUncompressed_ = maxUncompressed;
  }

  uint32_t getMaxUncompressed() const {
    return maxUncompressed_;
  }

  /**
   * set the stats object
   */
  void setStats(Stats* stats) {
    stats_ = stats;
  }

 protected:

  compress::HeaderPieceList outHeaders_;
  uint32_t encodeHeadroom_{0};
  HTTPHeaderSize encodedSize_;
  HTTPHeaderSize decodedSize_;
  uint32_t maxUncompressed_{kMaxUncompressed};
  Stats* stats_{nullptr};
  HeaderCodec::StreamingCallback* streamingCb_{nullptr};
};

}
