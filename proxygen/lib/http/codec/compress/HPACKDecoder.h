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
#include <proxygen/lib/http/codec/compress/HPACKStreamingCallback.h>
#include <proxygen/lib/http/codec/compress/HPACKContext.h>
#include <proxygen/lib/http/codec/compress/HPACKDecoderBase.h>
#include <proxygen/lib/http/codec/compress/HPACKDecodeBuffer.h>

namespace proxygen {

class HPACKDecoder : public HPACKDecoderBase,
                     public HPACKContext {
 public:
  explicit HPACKDecoder(
    uint32_t tableSize=HPACK::kTableSize,
    uint32_t maxUncompressed=HeaderCodec::kMaxUncompressed)
      : HPACKDecoderBase(tableSize, maxUncompressed),
        HPACKContext(tableSize) {}

  /**
   * given a Cursor and a total amount of bytes we can consume from it,
   * decode headers and invoke a callback.
   */
  void decodeStreaming(folly::io::Cursor& cursor,
                       uint32_t totalBytes,
                       HPACK::StreamingCallback* streamingCb);

 private:
  bool isValid(uint32_t index);

  uint32_t decodeIndexedHeader(HPACKDecodeBuffer& dbuf,
                               HPACK::StreamingCallback* streamingCb,
                               headers_t* emitted);

  uint32_t decodeLiteralHeader(HPACKDecodeBuffer& dbuf,
                               HPACK::StreamingCallback* streamingCb,
                               headers_t* emitted);

  uint32_t decodeHeader(HPACKDecodeBuffer& dbuf,
                        HPACK::StreamingCallback* streamingCb,
                        headers_t* emitted);
};

}
