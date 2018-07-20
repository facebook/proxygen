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
#include <folly/Expected.h>
#include <folly/Function.h>

namespace proxygen {

class TestStreamingCallback : public HPACK::StreamingCallback {
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
  void onDecodeError(HPACK::DecodeError decodeError) override {
    error = decodeError;
  }

  void reset() {
    headers.clear();
    error = HPACK::DecodeError::NONE;
  }

  folly::Expected<HeaderDecodeResult, HPACK::DecodeError> getResult() {
    if (error == HPACK::DecodeError::NONE) {
      return HeaderDecodeResult{headers, 0};
    } else {
      return folly::makeUnexpected(error);
    }
  }

  bool hasError() const {
    return error != HPACK::DecodeError::NONE;
  }

  std::unique_ptr<std::vector<HPACKHeader>> hpackHeaders() const {
    CHECK(!hasError());
    auto result = std::make_unique<std::vector<HPACKHeader>>();
    for (size_t i = 0; i < headers.size(); i += 2) {
      result->emplace_back(headers[i].str, headers[i + 1].str);
    }

    return result;
  }


  compress::HeaderPieceList headers;
  HPACK::DecodeError error{HPACK::DecodeError::NONE};
  char* duplicate(const folly::fbstring& str) {
    char* res = CHECK_NOTNULL(new char[str.length() + 1]);
    memcpy(res, str.data(), str.length() + 1);
    return res;
  }

  folly::Function<void()> headersCompleteCb;
};

}
