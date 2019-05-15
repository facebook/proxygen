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

#include <proxygen/lib/http/session/HQDownstreamSession.h>

namespace proxygen {

void HQDownstreamSession::onTransportReady() noexcept {
  HQDownstreamSession::DestructorGuard dg(this);
  if (!onTransportReadyCommon()) {
    return;
  }
  if (infoCallback_) {
    infoCallback_->onTransportReady(*this);
  }
}

void HQDownstreamSession::onConnectionErrorHandler(
    std::pair<quic::QuicErrorCode, std::string> /* error */) noexcept {
  if (infoCallback_) {
    infoCallback_->onConnectionError(*this);
  }
}

void HQDownstreamSession::setupOnHeadersComplete(HTTPTransaction* txn,
                                                 HTTPMessage* msg) {
  HTTPTransaction::Handler* handler =
      getController()->getRequestHandler(*txn, msg);
  CHECK(handler);
  txn->setHandler(handler);
  setNewTransactionPauseState(txn);
  if (infoCallback_) {
    infoCallback_->onIngressMessage(*this, *msg);
  }
}

bool HQDownstreamSession::isDetachable(bool) const {
  LOG(FATAL) << __func__ << " is an upstream interface";
  return false;
}

void HQDownstreamSession::attachThreadLocals(folly::EventBase*,
                                             folly::SSLContextPtr,
                                             const WheelTimerInstance&,
                                             HTTPSessionStats*,
                                             FilterIteratorFn,
                                             HeaderCodec::Stats*,
                                             HTTPSessionController*) {
  LOG(FATAL) << __func__ << " is an upstream interface";
}

void HQDownstreamSession::detachThreadLocals(bool) {
  LOG(FATAL) << __func__ << " is an upstream interface";
}

} // namespace proxygen
