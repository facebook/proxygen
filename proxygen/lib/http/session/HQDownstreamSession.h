/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <proxygen/lib/http/session/HQSession.h>

namespace proxygen {

class HQDownstreamSession : public HQSession {
 public:
  HQDownstreamSession(const std::chrono::milliseconds transactionsTimeout,
                      HTTPSessionController* controller,
                      const wangle::TransportInfo& tinfo,
                      InfoCallback* sessionInfoCb)
      : HQSession(transactionsTimeout,
                  controller,
                  proxygen::TransportDirection::DOWNSTREAM,
                  tinfo,
                  sessionInfoCb) {
  }

  void onWriteCipherAvailable() noexcept override;

  void onTransportReady() noexcept override;

  void onFullHandshakeDone() noexcept override;

  void onAppRateLimited() noexcept override;

  HTTPTransaction::Handler* getTransactionTimeoutHandler(
      HTTPTransaction* txn) override {
    return getController()->getTransactionTimeoutHandler(txn,
                                                         getLocalAddress());
  }

  void setupOnHeadersComplete(HTTPTransaction* txn, HTTPMessage* msg) override;

  void onConnectionSetupErrorHandler(quic::QuicError) noexcept override;

  bool isDetachable(bool) const override;

  void attachThreadLocals(folly::EventBase*,
                          std::shared_ptr<const folly::SSLContext>,
                          const WheelTimerInstance&,
                          HTTPSessionStats*,
                          FilterIteratorFn,
                          HeaderCodec::Stats*,
                          HTTPSessionController*) override;

  void detachThreadLocals(bool) override;

  bool isReplaySafe() const override {
    LOG(FATAL) << __func__ << " is an upstream interface";
  }

  /**
   * Returns true iff a new outgoing transaction can be made on this session
   */
  bool supportsMoreTransactions() const override {
    return sock_ && sock_->getNumOpenableUnidirectionalStreams() &&
           HTTPSessionBase::supportsMoreTransactions();
  }

  uint32_t getNumOutgoingStreams() const override {
    return 0;
  }

  uint32_t getNumIncomingStreams() const override {
    // need transport API
    return static_cast<uint32_t>(streams_.size());
  }

  folly::Optional<HTTPHeaders> getExtraHeaders(
      const HTTPMessage& haeders, quic::StreamId streamId) override;

 protected:
  ~HQDownstreamSession() override {
    CHECK_EQ(getNumStreams(), 0);
  }

  void dispatchPushStream(quic::StreamId /* pushStreamId */,
                          hq::PushId /* pushId */,
                          size_t /* toConsume */) override {
    LOG(DFATAL) << "nope";
  }

  // Whether or not we have already received an onTransportReady callback.
  bool transportReadyNotified_{false};
};

} // namespace proxygen
