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

#include <folly/Expected.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/codec/HeaderConstants.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/HPACKStreamingCallback.h>

namespace proxygen { namespace compress {
class SimStreamingCallback : public HPACK::StreamingCallback {
 public:
  SimStreamingCallback(uint16_t index,
                       std::function<void(std::chrono::milliseconds)> cb,
                       bool isP=false)
      : requestIndex(index), headersCompleteCb(cb), isPublic(isP) {
  }

  SimStreamingCallback(SimStreamingCallback&& goner) noexcept {
    std::swap(msg, goner.msg);
    requestIndex = goner.requestIndex;
    seqn = goner.seqn;
    error = goner.error;
    std::swap(headersCompleteCb, goner.headersCompleteCb);
  }

  void onHeader(const folly::fbstring& name,
                const folly::fbstring& value) override {
    if (name[0] == ':' && !isPublic) {
      if (name == headers::kMethod) {
        msg.setMethod(value);
      } else if (name == headers::kScheme) {
        if (value == headers::kHttps) {
          msg.setSecure(true);
        }
      } else if (name == headers::kAuthority) {
        msg.getHeaders().add(HTTP_HEADER_HOST, value.toStdString());
      } else if (name == headers::kPath) {
        msg.setURL(value.toStdString());
      } else {
        DCHECK(false) << "Bad header name=" << name << " value=" << value;
      }
    } else {
      msg.getHeaders().add(name.toStdString(), value.toStdString());
    }
  }

  void onHeadersComplete(HTTPHeaderSize) override {
    auto combinedCookie = msg.getHeaders().combine(HTTP_HEADER_COOKIE, "; ");
    if (!combinedCookie.empty()) {
      msg.getHeaders().set(HTTP_HEADER_COOKIE, combinedCookie);
    }
    std::chrono::milliseconds holDelay(0);
    if (holStart != TimeUtil::getZeroTimePoint()) {
      holDelay = millisecondsSince(holStart);
    }
    complete = true;
    if (headersCompleteCb) {
      headersCompleteCb(holDelay);
    }
  }

  void onDecodeError(HPACK::DecodeError decodeError) override {
    error = decodeError;
    DCHECK(false) << "Unexpected error in simulator";
  }

  folly::Expected<proxygen::HTTPMessage*, HPACK::DecodeError> getResult() {
    if (error == HPACK::DecodeError::NONE) {
      return &msg;
    } else {
      return folly::makeUnexpected(error);
    }
  }

  void maybeMarkHolDelay() {
    if (!complete) {
      holStart = getCurrentTime();
    }
  }

  // Global index (across all domains)
  uint16_t requestIndex{0};
  // Per domain request sequence number
  uint16_t seqn{0};
  HPACK::DecodeError error{HPACK::DecodeError::NONE};
  proxygen::HTTPMessage msg;
  std::function<void(std::chrono::milliseconds)> headersCompleteCb;
  TimePoint holStart{TimeUtil::getZeroTimePoint()};
  bool complete{false};
  bool isPublic{false};
};

}} // namespace proxygen::compress
