/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <proxygen/lib/http/session/HQSession.h>

#include <folly/io/async/HHWheelTimer.h>
#include <quic/common/events/FollyQuicEventBase.h>

namespace proxygen {

class HQUpstreamSession : public HQSession {
  class ConnectTimeout;

  enum class ConnCallbackState { NONE, CONNECT_SUCCESS, REPLAY_SAFE, DONE };

 public:
  HQUpstreamSession(const std::chrono::milliseconds transactionsTimeout,
                    const std::chrono::milliseconds connectTimeoutMs,
                    HTTPSessionController* controller,
                    const wangle::TransportInfo& tinfo,
                    InfoCallback* sessionInfoCb)
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

  void onFirstPeerPacketProcessed() noexcept override;

  void handleReplaySafe() noexcept;

  HTTPTransaction::Handler* getTransactionTimeoutHandler(
      HTTPTransaction* /* txn */) override {
    // No special handler for upstream requests that time out
    return nullptr;
  }

  void setupOnHeadersComplete(HTTPTransaction* /* txn */,
                              HTTPMessage* /* msg */) override {
  }

  void onConnectionSetupErrorHandler(quic::QuicError code) noexcept override;

  bool isDetachable(bool checkSocket) const override;

  void attachThreadLocals(folly::EventBase*,
                          std::shared_ptr<const folly::SSLContext>,
                          const WheelTimerInstance&,
                          HTTPSessionStats*,
                          FilterIteratorFn,
                          HeaderCodec::Stats*,
                          HTTPSessionController*) override;

  void detachThreadLocals(bool) override;

  /**
   * Returns true iff a new outgoing transaction can be made on this session
   */
  bool supportsMoreTransactions() const override {
    return sock_ && sock_->getNumOpenableBidirectionalStreams() &&
           HTTPSessionBase::supportsMoreTransactions();
  }

  uint32_t getNumOutgoingStreams() const override {
    // need transport API
    return static_cast<uint32_t>(streams_.size());
  }

  uint32_t getNumIncomingStreams() const override {
    return 0;
  }

 protected:
  ~HQUpstreamSession() override;

 private:
  void connectTimeoutExpired() noexcept;

  void dispatchPushStream(quic::StreamId /* pushStreamId */,
                          hq::PushId /* pushId */,
                          size_t /* toConsume */) override;

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
