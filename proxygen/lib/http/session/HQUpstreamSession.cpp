/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/session/HQUpstreamSession.h>
#include <quic/common/events/FollyQuicEventBase.h>
#include <wangle/acceptor/ConnectionManager.h>

namespace proxygen {

HQUpstreamSession::~HQUpstreamSession() {
  CHECK_EQ(getNumStreams(), 0);
}

void HQUpstreamSession::startNow() {
  HQSession::startNow();
  if (connectCb_ && connectTimeoutMs_.count() > 0) {
    // Start a timer in case the connection takes too long.
    getEventBase()->timer().scheduleTimeout(&connectTimeout_,
                                            connectTimeoutMs_);
  }
}

void HQUpstreamSession::connectTimeoutExpired() noexcept {
  VLOG(4) << __func__ << " sess=" << *this << ": connection failed";
  if (connectCb_) {
    onConnectionError(quic::QuicError(quic::LocalErrorCode::CONNECT_FAILED,
                                      "connect timeout"));
  }
}

void HQUpstreamSession::onTransportReady() noexcept {
  HQUpstreamSession::DestructorGuard dg(this);
  if (!HQSession::onTransportReadyCommon()) {
    // Something went wrong in onTransportReady, e.g. the ALPN is not supported
    return;
  }
  connectSuccess();
}

void HQUpstreamSession::onFirstPeerPacketProcessed() noexcept {
  HQUpstreamSession::DestructorGuard dg(this);
  if (connectCb_) {
    connectCb_->onFirstPeerPacketProcessed();
  }
}

void HQUpstreamSession::connectSuccess() noexcept {
  HQUpstreamSession::DestructorGuard dg(this);
  if (connectCb_) {
    connectCb_->connectSuccess();
  }
  if (connCbState_ == ConnCallbackState::REPLAY_SAFE) {
    handleReplaySafe();
    connCbState_ = ConnCallbackState::DONE;
  } else {
    connCbState_ = ConnCallbackState::CONNECT_SUCCESS;
  }
}

void HQUpstreamSession::onReplaySafe() noexcept {
  HQUpstreamSession::DestructorGuard dg(this);
  if (connCbState_ == ConnCallbackState::CONNECT_SUCCESS) {
    handleReplaySafe();
    connCbState_ = ConnCallbackState::DONE;
  } else {
    connCbState_ = ConnCallbackState::REPLAY_SAFE;
  }
}

void HQUpstreamSession::handleReplaySafe() noexcept {
  HQSession::onReplaySafe();
  // In the case that zero rtt, onTransportReady is almost called
  // immediately without proof of network reachability, and onReplaySafe is
  // expected to be called in 1 rtt time (if success).
  if (connectCb_) {
    auto cb = connectCb_;
    connectCb_ = nullptr;
    connectTimeout_.cancelTimeout();
    cb->onReplaySafe();
  }
}

void HQUpstreamSession::onConnectionEnd() noexcept {
  VLOG(4) << __func__ << " sess=" << *this;

  HQSession::DestructorGuard dg(this);
  if (connectCb_) {
    onConnectionSetupErrorHandler(quic::QuicError(
        quic::LocalErrorCode::CONNECT_FAILED, "session destroyed"));
  }
  HQSession::onConnectionEnd();
}

void HQUpstreamSession::onConnectionSetupErrorHandler(
    quic::QuicError code) noexcept {
  // For an upstream connection, any error before onTransportReady gets
  // notified as a connect error.
  if (connectCb_) {
    HQSession::DestructorGuard dg(this);
    auto cb = connectCb_;
    connectCb_ = nullptr;
    cb->connectError(std::move(code));
    connectTimeout_.cancelTimeout();
  }
}

bool HQUpstreamSession::isDetachable(bool checkSocket) const {
  VLOG(4) << __func__ << " sess=" << *this;
  // TODO: deal with control streams in h2q
  if (checkSocket && sock_ && !sock_->isDetachable()) {
    return false;
  }
  return getNumOutgoingStreams() == 0 && getNumIncomingStreams() == 0;
}

void HQUpstreamSession::attachThreadLocals(
    folly::EventBase* eventBase,
    std::shared_ptr<const folly::SSLContext>,
    const WheelTimerInstance& timeout,
    HTTPSessionStats* stats,
    FilterIteratorFn fn,
    HeaderCodec::Stats* headerCodecStats,
    HTTPSessionController* controller) {
  // TODO: deal with control streams in h2q
  VLOG(4) << __func__ << " sess=" << *this;
  setController(controller);
  setSessionStats(stats);
  auto qEvbWrapper = std::make_shared<quic::FollyQuicEventBase>(eventBase);
  if (sock_) {
    sock_->attachEventBase(std::move(qEvbWrapper));
  }
  codec_.foreach (fn);
  setHeaderCodecStats(headerCodecStats);
  scheduleInLoop(getEventBase());
  // The caller MUST re-add the connection to a new connection manager.
}

void HQUpstreamSession::detachThreadLocals(bool) {
  VLOG(4) << __func__ << " sess=" << *this;
  // TODO: deal with control streams in h2q
  CHECK_EQ(getNumOutgoingStreams(), 0);
  cancelLoopCallback();
  cancelSlowConsumerTimer();

  // TODO: Pause reads and invoke infocallback
  // pauseReadsImpl();
  if (sock_) {
    sock_->detachEventBase();
  }

  setController(nullptr);
  setSessionStats(nullptr);
  // The codec filters *shouldn't* be accessible while the socket is detached,
  // I hope
  setHeaderCodecStats(nullptr);
  auto cm = getConnectionManager();
  if (cm) {
    cm->removeConnection(this);
  }
}

void HQUpstreamSession::dispatchPushStream(quic::StreamId pushStreamId,
                                           hq::PushId,
                                           size_t) {
  // http/3 push in proxygen/lib is not supported
  rejectStream(pushStreamId);
}

} // namespace proxygen
