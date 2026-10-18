/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/session/HQDownstreamSession.h>

namespace proxygen {

void HQDownstreamSession::onWriteCipherAvailable() noexcept {
  HQDownstreamSession::DestructorGuard dg(this);
  onWriteCipherAvailableCommon();
}

void HQDownstreamSession::onTransportReady() noexcept {
  HQDownstreamSession::DestructorGuard dg(this);
  if (!onTransportReadyCommon()) {
    return;
  }
  if (infoCallback_) {
    infoCallback_->onTransportReady(*this);
  }
  transportReadyNotified_ = true;
}

void HQDownstreamSession::onFullHandshakeDone() noexcept {
  HQDownstreamSession::DestructorGuard dg(this);
  if (infoCallback_) {
    infoCallback_->onFullHandshakeCompletion(*this);
  }
}

void HQDownstreamSession::onAppRateLimited() noexcept {
  invokeOnEgressStreams(([](HQStreamTransportBase* stream) {
                          stream->txn_.onEgressTransportAppRateLimited();
                        }),
                        false /* includeDetached */);
}

void HQDownstreamSession::onConnectionSetupErrorHandler(
    quic::QuicError /* error */) noexcept {
  // Currently the users of this callback treat it like a connect error,
  // not a general connection error. Since we don't have proper separation
  // suppress the errors after onTransportReady has happened.
  if (infoCallback_ && !transportReadyNotified_) {
    infoCallback_->onConnectionError(*this);
  }
}

void HQDownstreamSession::setupOnHeadersComplete(HTTPTransaction* txn,
                                                 HTTPMessage* msg) {
  HTTPTransaction::Handler* handler =
      getController()->getRequestHandler(*txn, msg);
  CHECK(handler);
  txn->setHandler(handler);
  if (infoCallback_) {
    infoCallback_->onIngressMessage(*this, *msg);
  }
}

bool HQDownstreamSession::isDetachable(bool) const {
  LOG(FATAL) << __func__ << " is an upstream interface";
}

void HQDownstreamSession::attachThreadLocals(
    folly::EventBase*,
    std::shared_ptr<const folly::SSLContext>,
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

folly::Optional<HTTPHeaders> HQDownstreamSession::getExtraHeaders(
    const HTTPMessage& headers, quic::StreamId streamId) {
  if (!sock_) {
    return folly::none;
  }
  if (headers.getHeaders().exists(HTTP_HEADER_PRIORITY)) {
    return folly::none;
  }
  auto priority = sock_->getStreamPriority(streamId);
  if (!priority) {
    return folly::none;
  }
  HTTPHeaders extraHeaders;
  quic::HTTPPriorityQueue::Priority httpPri(*priority);
  extraHeaders.add(HTTP_HEADER_PRIORITY,
                   httpPriorityToString(HTTPPriority(httpPri->urgency,
                                                     httpPri->incremental,
                                                     httpPri->order,
                                                     httpPri->paused)));
  return extraHeaders;
}

} // namespace proxygen
