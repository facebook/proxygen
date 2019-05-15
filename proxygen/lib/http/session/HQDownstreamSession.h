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

namespace proxygen {

class HQDownstreamSession : public HQSession {
 public:
  HQDownstreamSession(const std::chrono::milliseconds transactionsTimeout,
                      HTTPSessionController* controller,
                      const wangle::TransportInfo& tinfo,
                      InfoCallback* sessionInfoCb,
                      folly::Function<void(HTTPCodecFilterChain& chain)>
                      /* codecFilterCallbackFn */
                      = nullptr)
      : HQSession(transactionsTimeout,
                  controller,
                  proxygen::TransportDirection::DOWNSTREAM,
                  tinfo,
                  sessionInfoCb) {
    egressSettings_.setSetting(SettingsId::_HQ_NUM_PLACEHOLDERS,
                               hq::kDefaultEgressNumPlaceHolders);
  }

  void onTransportReady() noexcept override;

  HTTPTransaction::Handler* getTransactionTimeoutHandler(
      HTTPTransaction* txn) override {
    return getController()->getTransactionTimeoutHandler(txn,
                                                         getLocalAddress());
  }

  void setupOnHeadersComplete(HTTPTransaction* txn, HTTPMessage* msg) override;

  void onConnectionErrorHandler(
      std::pair<quic::QuicErrorCode, std::string>) noexcept override;

  bool isDetachable(bool) const override;

  void attachThreadLocals(folly::EventBase*,
                          folly::SSLContextPtr,
                          const WheelTimerInstance&,
                          HTTPSessionStats*,
                          FilterIteratorFn,
                          HeaderCodec::Stats*,
                          HTTPSessionController*) override;

  void detachThreadLocals(bool) override;

  bool isReplaySafe() const override {
    LOG(FATAL) << __func__ << " is an upstream interface";
    return false;
  }

 private:
  ~HQDownstreamSession() override {
  }
};

} // namespace proxygen
