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

#include <proxygen/lib/http/codec/compress/HeaderCodec.h>

namespace proxygen {

class TestStreamingCallback : public HeaderCodec::StreamingCallback {
 public:
  void onHeader(const folly::fbstring& name,
                const folly::fbstring& value) override {
    headers.emplace_back(duplicate(name), name.size(), true, false);
    headers.emplace_back(duplicate(value), value.size(), true, false);
  }
  void onHeadersComplete(HTTPHeaderSize /*decodedSize*/) override {
    if (headersCompleteCb) {
      headersCompleteCb();
    }
  }
  void onDecodeError(HeaderDecodeError decodeError) override {
    error = decodeError;
  }

  void reset() {
    headers.clear();
    error = HeaderDecodeError::NONE;
  }

  Result<HeaderDecodeResult, HeaderDecodeError> getResult() {
    if (error == HeaderDecodeError::NONE) {
      return HeaderDecodeResult{headers, 0};
    } else {
      return error;
    }
  }

  compress::HeaderPieceList headers;
  HeaderDecodeError error{HeaderDecodeError::NONE};
  char* duplicate(const folly::fbstring& str) {
    char* res = CHECK_NOTNULL(new char[str.length() + 1]);
    memcpy(res, str.data(), str.length() + 1);
    return res;
  }

  std::function<void()> headersCompleteCb;
};

}
