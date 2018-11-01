/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/HPACKDecodeBuffer.h>
#include <proxygen/lib/http/codec/compress/HPACKHeader.h>
#include <proxygen/lib/http/codec/compress/HPACKStreamingCallback.h>

namespace proxygen {

class HeaderTable;

/**
 * Common decoder functionality between HPACK and QPACK
 */
class HPACKDecoderBase {
 public:
  HPACKDecoderBase(
      uint32_t tableSize,
      uint32_t maxUncompressed)
      : maxTableSize_(tableSize)
      , maxUncompressed_(maxUncompressed) {}

  using headers_t = std::vector<HPACKHeader>;

  HPACK::DecodeError getError() const {
    return err_;
  }

  bool hasError() const {
    return err_ != HPACK::DecodeError::NONE;
  }

  void setHeaderTableMaxSize(uint32_t maxSize) {
    maxTableSize_ = maxSize;
  }

  void setMaxUncompressed(uint64_t maxUncompressed) {
    maxUncompressed_ = maxUncompressed;
  }

 protected:
  uint32_t emit(const HPACKHeader& header,
                HPACK::StreamingCallback* streamingCb,
                headers_t* emitted);

  void completeDecode(
      HeaderCodec::Type type,
      HPACK::StreamingCallback* streamingCb,
      uint32_t compressedSize,
      uint32_t emittedSize);

  void handleTableSizeUpdate(HPACKDecodeBuffer& dbuf, HeaderTable& table);

  HPACK::DecodeError err_{HPACK::DecodeError::NONE};
  uint32_t maxTableSize_;
  uint64_t maxUncompressed_;
};

}
