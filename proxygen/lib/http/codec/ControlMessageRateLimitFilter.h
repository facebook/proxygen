/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/io/async/HHWheelTimer.h>
#include <proxygen/lib/http/codec/HTTP2Framer.h>
#include <proxygen/lib/http/codec/HTTPCodecFilter.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>

namespace proxygen {

// These constants define the rate at which we limit certain events.
constexpr uint32_t kDefaultMaxControlMsgsPerInterval = 500;
constexpr uint32_t kMaxControlMsgsPerIntervalLowerBound = 100;
constexpr std::chrono::milliseconds kDefaultControlMsgDuration{100};

constexpr uint32_t kDefaultMaxDirectErrorHandlingPerInterval = 100;
constexpr uint32_t kMaxDirectErrorHandlingPerIntervalLowerBound = 50;
constexpr std::chrono::milliseconds kDefaultDirectErrorHandlingDuration{100};

constexpr uint32_t kDefaultMaxHeadersPerInterval = 500;
constexpr uint32_t kMaxHeadersPerIntervalLowerBound = 100;
constexpr std::chrono::milliseconds kDefaultHeadersDuration{100};

enum RateLimitTarget {
  CONTROL_MSGS,
  DIRECT_ERROR_HANDLING,
  HEADERS,
};

/**
 * This class implements the rate limiting logic for control messages and
 * stream errors (that might produce HTTP error pages).  If a rate limit is
 * exeeded, the callback is converted to a session level error with
 * ProxygenError = kErrorDropped.  This is a signal to the codec callback that
 * the codec would like the connection dropped.
 *
 * TODO: Refactor this into separate filters, or group related parameters
 * into structs.
 */
class ControlMessageRateLimitFilter : public PassThroughHTTPCodecFilter {
 public:
  explicit ControlMessageRateLimitFilter(folly::HHWheelTimer* timer,
                                         HTTPSessionStats* httpSessionStats)
      : resetControlMessages_(numControlMsgsInCurrentInterval_,
                              RateLimitTarget::CONTROL_MSGS,
                              httpSessionStats),
        resetDirectErrors_(numDirectErrorHandlingInCurrentInterval_,
                           RateLimitTarget::DIRECT_ERROR_HANDLING,
                           httpSessionStats),
        resetHeaders_(numHeadersInCurrentInterval_,
                      RateLimitTarget::HEADERS,
                      httpSessionStats),
        timer_(timer),
        httpSessionStats_(httpSessionStats) {
  }

  void setSessionStats(HTTPSessionStats* httpSessionStats) {
    httpSessionStats_ = httpSessionStats;
    resetControlMessages_.httpSessionStats = httpSessionStats;
    resetHeaders_.httpSessionStats = httpSessionStats;
  }

  void setParams(uint32_t maxControlMsgsPerInterval,
                 uint32_t maxDirectErrorHandlingPerInterval,
                 uint32_t maxHeadersPerInterval,
                 std::chrono::milliseconds controlMsgIntervalDuration,
                 std::chrono::milliseconds directErrorHandlingIntervalDuration,
                 std::chrono::milliseconds headersIntervalDuration) {
    maxControlMsgsPerInterval_ = maxControlMsgsPerInterval;
    maxDirectErrorHandlingPerInterval_ = maxDirectErrorHandlingPerInterval;
    controlMsgIntervalDuration_ = controlMsgIntervalDuration;
    directErrorHandlingIntervalDuration_ = directErrorHandlingIntervalDuration;
    maxHeadersPerInterval_ = maxHeadersPerInterval;
    headersIntervalDuration_ = headersIntervalDuration;
  }

  // Filter functions
  void onAbort(HTTPCodec::StreamID streamID, ErrorCode code) override {
    if (!incrementNumControlMsgsInCurInterval(http2::FrameType::RST_STREAM)) {
      callback_->onAbort(streamID, code);
    }
  }
  void onPingRequest(uint64_t data) override {
    if (!incrementNumControlMsgsInCurInterval(http2::FrameType::PING)) {
      callback_->onPingRequest(data);
    }
  }
  void onSettings(const SettingsList& settings) override {
    if (!incrementNumControlMsgsInCurInterval(http2::FrameType::SETTINGS)) {
      callback_->onSettings(settings);
    }
  }
  void onPriority(HTTPCodec::StreamID streamID,
                  const HTTPMessage::HTTP2Priority& pri) override {
    if (!incrementNumControlMsgsInCurInterval(http2::FrameType::PRIORITY)) {
      callback_->onPriority(streamID, pri);
    }
  }

  void onPriority(StreamID streamID, const HTTPPriority& pri) override {
    if (!incrementNumControlMsgsInCurInterval(http2::FrameType::PRIORITY)) {
      callback_->onPriority(streamID, pri);
    }
  }

  void onHeadersComplete(StreamID stream,
                         std::unique_ptr<HTTPMessage> msg) override {
    if (!incrementNumHeadersInCurInterval()) {
      callback_->onHeadersComplete(stream, std::move(msg));
    }
  }

  void onError(HTTPCodec::StreamID streamID,
               const HTTPException& error,
               bool newTxn) override {
    // We only rate limit stream errors with no codec status code.
    // These may trigger a direct HTTP response.
    if (streamID == 0 || error.hasCodecStatusCode() ||
        !incrementDirectErrorHandlingInCurInterval()) {
      callback_->onError(streamID, error, newTxn);
    }
  }

  void attachThreadLocals(folly::HHWheelTimer* timer) {
    timer_ = timer;
  }

  void detachThreadLocals() {
    resetControlMessages_.cancelTimeout();
    resetDirectErrors_.cancelTimeout();
    resetHeaders_.cancelTimeout();
    timer_ = nullptr;
    // Free pass when switching threads
    numControlMsgsInCurrentInterval_ = 0;
    numDirectErrorHandlingInCurrentInterval_ = 0;
  }

 private:
  bool incrementNumControlMsgsInCurInterval(http2::FrameType frameType) {
    if (numControlMsgsInCurrentInterval_ == 0) {
      // The first control message (or first after a reset) schedules the next
      // reset timer
      CHECK(timer_);
      timer_->scheduleTimeout(&resetControlMessages_,
                              controlMsgIntervalDuration_);
    }

    if (++numControlMsgsInCurrentInterval_ > maxControlMsgsPerInterval_) {
      if (httpSessionStats_) {
        httpSessionStats_->recordControlMsgRateLimited();
      }
      HTTPException ex(
          HTTPException::Direction::INGRESS_AND_EGRESS,
          folly::to<std::string>(
              "dropping connection due to too many control messages, num "
              "control messages = ",
              numControlMsgsInCurrentInterval_,
              ", most recent frame type = ",
              getFrameTypeString(frameType)));
      ex.setProxygenError(kErrorDropped);
      callback_->onError(0, ex, true);
      return true;
    }

    return false;
  }

  bool incrementNumHeadersInCurInterval() {
    if (numHeadersInCurrentInterval_ == 0) {
      // The first header (or first after a reset) schedules the next
      // reset timer
      CHECK(timer_);
      timer_->scheduleTimeout(&resetHeaders_, headersIntervalDuration_);
    }

    if (++numHeadersInCurrentInterval_ > maxHeadersPerInterval_) {
      if (httpSessionStats_) {
        httpSessionStats_->recordHeadersRateLimited();
      }
      callback_->onGoaway(http2::kMaxStreamID, ErrorCode::NO_ERROR);
      return true;
    }

    return false;
  }

  bool incrementDirectErrorHandlingInCurInterval() {
    if (numDirectErrorHandlingInCurrentInterval_ == 0) {
      // The first control message (or first after a reset) schedules the next
      // reset timer
      CHECK(timer_);
      timer_->scheduleTimeout(&resetDirectErrors_,
                              directErrorHandlingIntervalDuration_);
    }

    if (++numDirectErrorHandlingInCurrentInterval_ >
        maxDirectErrorHandlingPerInterval_) {
      HTTPException ex(
          HTTPException::Direction::INGRESS_AND_EGRESS,
          folly::to<std::string>(
              "dropping connection due to too many newly created txns  when "
              "directly handling errors, num direct error handling cases = ",
              numDirectErrorHandlingInCurrentInterval_));
      ex.setProxygenError(kErrorDropped);
      callback_->onError(0, ex, true);
      return true;
    }

    return false;
  }

  class ResetCounterTimeout : public folly::HHWheelTimer::Callback {
   public:
    explicit ResetCounterTimeout(uint32_t& counterIn,
                                 RateLimitTarget rateLimitTargetIn,
                                 HTTPSessionStats* httpSessionStatsIn = nullptr)
        : counter(counterIn),
          rateLimitTarget(rateLimitTargetIn),
          httpSessionStats(httpSessionStatsIn) {
    }

    void timeoutExpired() noexcept override {
      if (counter > 0 && httpSessionStats) {
        switch (rateLimitTarget) {
          case RateLimitTarget::CONTROL_MSGS:
            httpSessionStats->recordControlMsgsInInterval(counter);
            break;
          case RateLimitTarget::DIRECT_ERROR_HANDLING:
            // No stats for this one
            break;
          case RateLimitTarget::HEADERS:
            httpSessionStats->recordHeadersInInterval(counter);
            break;
        }
      }
      counter = 0;
    }
    void callbackCanceled() noexcept override {
    }

    uint32_t& counter;
    RateLimitTarget rateLimitTarget;
    HTTPSessionStats* httpSessionStats{nullptr};
  };

  /**
   * The two variables below keep track of the number of Control messages,
   * and the number of error handling events that are handled by a newly
   * created transaction handler seen in the current interval, respectively.
   */
  uint32_t numControlMsgsInCurrentInterval_{0};
  uint32_t maxControlMsgsPerInterval_{kDefaultMaxControlMsgsPerInterval};

  uint32_t numDirectErrorHandlingInCurrentInterval_{0};
  uint32_t maxDirectErrorHandlingPerInterval_{
      kDefaultMaxDirectErrorHandlingPerInterval};

  uint32_t numHeadersInCurrentInterval_{0};
  uint32_t maxHeadersPerInterval_{kDefaultMaxHeadersPerInterval};

  std::chrono::milliseconds controlMsgIntervalDuration_{
      kDefaultControlMsgDuration};
  std::chrono::milliseconds directErrorHandlingIntervalDuration_{
      kDefaultDirectErrorHandlingDuration};
  std::chrono::milliseconds headersIntervalDuration_{kDefaultHeadersDuration};

  ResetCounterTimeout resetControlMessages_;
  ResetCounterTimeout resetDirectErrors_;
  ResetCounterTimeout resetHeaders_;
  folly::HHWheelTimer* timer_{nullptr};
  HTTPSessionStats* httpSessionStats_{nullptr};
};

} // namespace proxygen
