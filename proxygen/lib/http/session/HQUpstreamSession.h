/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once
#include <proxygen/lib/http/session/HQSession.h>

#include <folly/io/async/HHWheelTimer.h>

namespace proxygen {

class HQUpstreamSession : public HQSession {
  class ConnectTimeout;

  enum class ConnCallbackState { NONE, CONNECT_SUCCESS, REPLAY_SAFE, DONE };

 public:
  HQUpstreamSession(const std::chrono::milliseconds transactionsTimeout,
                    const std::chrono::milliseconds connectTimeoutMs,
                    HTTPSessionController* controller,
                    const wangle::TransportInfo& tinfo,
                    InfoCallback* sessionInfoCb,
                    folly::Function<void(HTTPCodecFilterChain& chain)>
                    /* codecFilterCallbackFn */
                    = nullptr)
      : HQSession(transactionsTimeout,
                  controller,
                  proxygen::TransportDirection::UPSTREAM,
                  tinfo,
                  sessionInfoCb),
        connectTimeoutMs_(connectTimeoutMs),
        connectTimeout_(*this) {
  }

  void setConnectCallback(ConnectCallback* connectCb) noexcept {
    connectCb_ = connectCb;
  }

  void connectSuccess() noexcept override;

  /**
   * Returns true if the underlying transport has completed full handshake.
   */
  bool isReplaySafe() const override {
    return sock_ ? sock_->replaySafe() : false;
  }

  void onConnectionEnd() noexcept override;

  void startNow() override;

  void onTransportReady() noexcept override;

  void onReplaySafe() noexcept override;

  void handleReplaySafe() noexcept;

  HTTPTransaction::Handler* getTransactionTimeoutHandler(
      HTTPTransaction* /* txn */) override {
    // No special handler for upstream requests that time out
    return nullptr;
  }

  void setupOnHeadersComplete(HTTPTransaction* /* txn */,
                              HTTPMessage* /* msg */) override {
  }

  void onConnectionErrorHandler(
      std::pair<quic::QuicErrorCode, std::string> code) noexcept override;

  bool isDetachable(bool checkSocket) const override;

  void attachThreadLocals(folly::EventBase*,
                          folly::SSLContextPtr,
                          const WheelTimerInstance&,
                          HTTPSessionStats*,
                          FilterIteratorFn,
                          HeaderCodec::Stats*,
                          HTTPSessionController*) override;

  void detachThreadLocals(bool) override;

 private:
  ~HQUpstreamSession() override;

  void connectTimeoutExpired() noexcept;

  /**
   * The Session notifies when an upstream 'connection' has been established
   * and it is possible to start creating new streams / sending data
   * The connection callback only expects either success or error
   * so it gets automatically reset to nullptr after the first invocation
   */
  ConnectCallback* connectCb_{nullptr};

  class ConnectTimeout : public folly::HHWheelTimer::Callback {
   public:
    explicit ConnectTimeout(HQUpstreamSession& session) : session_(session) {
    }

    void timeoutExpired() noexcept override {
      session_.connectTimeoutExpired();
    }

   private:
    HQUpstreamSession& session_;
  };

  std::chrono::milliseconds connectTimeoutMs_;
  ConnectTimeout connectTimeout_;
  ConnCallbackState connCbState_{ConnCallbackState::NONE};
};

} // namespace proxygen
