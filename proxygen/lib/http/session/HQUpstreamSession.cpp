/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/session/HQUpstreamSession.h>
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
    onConnectionError(std::make_pair(quic::LocalErrorCode::CONNECT_FAILED,
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
    onConnectionErrorHandler(std::make_pair(
        quic::LocalErrorCode::CONNECT_FAILED, "session destroyed"));
  }
  HQSession::onConnectionEnd();
}

void HQUpstreamSession::onConnectionErrorHandler(
    std::pair<quic::QuicErrorCode, std::string> code) noexcept {
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

void HQUpstreamSession::attachThreadLocals(folly::EventBase* eventBase,
                                           folly::SSLContextPtr,
                                           const WheelTimerInstance& timeout,
                                           HTTPSessionStats* stats,
                                           FilterIteratorFn fn,
                                           HeaderCodec::Stats* headerCodecStats,
                                           HTTPSessionController* controller) {
  // TODO: deal with control streams in h2q
  VLOG(4) << __func__ << " sess=" << *this;
  txnEgressQueue_.attachThreadLocals(timeout);
  setController(controller);
  setSessionStats(stats);
  if (sock_) {
    sock_->attachEventBase(eventBase);
  }
  codec_.foreach (fn);
  setHeaderCodecStats(headerCodecStats);
  sock_->getEventBase()->runInLoop(this);
  // The caller MUST re-add the connection to a new connection manager.
}

void HQUpstreamSession::detachThreadLocals(bool) {
  VLOG(4) << __func__ << " sess=" << *this;
  // TODO: deal with control streams in h2q
  CHECK_EQ(getNumOutgoingStreams(), 0);
  cancelLoopCallback();

  // TODO: Pause reads and invoke infocallback
  // pauseReadsImpl();
  if (sock_) {
    sock_->detachEventBase();
  }

  txnEgressQueue_.detachThreadLocals();
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

void HQUpstreamSession::onNetworkSwitch(
    std::unique_ptr<folly::AsyncUDPSocket> newSock) noexcept {
  if (sock_) {
    sock_->onNetworkSwitch(std::move(newSock));
  }
}

bool HQUpstreamSession::tryBindIngressStreamToTxn(
    hq::PushId pushId,
    HQIngressPushStream* pushStream) {
  // lookup pending nascent stream id

  VLOG(4) << __func__
          << " attempting to find pending stream id for pushID=" << pushId;
  auto lookup = streamLookup_.by<push_id>();
  VLOG(4) << __func__ << " lookup table contains " << lookup.size()
          << " elements";
  auto res = lookup.find(pushId);
  if (res == lookup.end()) {
    VLOG(4) << __func__ << " pushID=" << pushId
            << " not found in the lookup table";
    return false;
  }
  auto streamId = res->get<quic_stream_id>();

  if (pushStream == nullptr) {
    VLOG(4) << __func__ << " ingress stream hint not passed.";
    pushStream = findIngressPushStreamByPushId(pushId);
    if (!pushStream) {
      VLOG(4) << __func__ << " ingress stream with pushID=" << pushId
              << " not found.";
      return false;
    }
  }

  VLOG(4) << __func__ << " attempting to bind streamID=" << streamId
          << " to pushID=" << pushId;
  pushStream->bindTo(streamId);

  // Check postconditions - the ingress push stream
  // should own both the push id and the stream id.
  // No nascent stream should own the stream id
  auto streamById = findIngressPushStream(streamId);
  auto streamByPushId = findIngressPushStreamByPushId(pushId);

  DCHECK_EQ(streamId, pushStream->getIngressStreamId());
  DCHECK(streamById) << "Ingress stream must be bound to the streamID="
                     << streamId;
  DCHECK(streamByPushId) << "Ingress stream must be found by the pushID="
                         << pushId;
  DCHECK_EQ(streamById, streamByPushId) << "Must be same stream";

  VLOG(4) << __func__ << " successfully bound streamID=" << streamId
          << " to pushID=" << pushId;
  return true;
}

HQUpstreamSession::HQStreamTransportBase*
HQUpstreamSession::createIngressPushStream(
    HTTPCodec::StreamID parentId, hq::PushId pushId) {

  // Check that a stream with this ID has not been created yet
  DCHECK(!findIngressPushStreamByPushId(pushId))
      << "Ingress stream with this push ID already exists pushID=" << pushId;

  auto matchPair = ingressPushStreams_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(pushId),
      std::forward_as_tuple(
          *this,
          pushId,
          parentId,
          getNumTxnServed(),
          WheelTimerInstance(transactionsTimeout_, getEventBase())));

  CHECK(matchPair.second) << "Emplacement failed, despite earlier "
                             "existence check.";

  auto newIngressPushStream = &matchPair.first->second;

  // If there is a nascent stream ready to be bound to the newly
  // created ingress stream, do it now.
  auto bound = tryBindIngressStreamToTxn(pushId, newIngressPushStream);

  VLOG(4) << "Successfully created new ingress push stream"
          << " pushID=" << pushId << " parentStreamID=" << parentId
          << " bound=" << bound << " streamID="
          << (bound ? newIngressPushStream->getIngressStreamId()
                    : static_cast<unsigned long>(-1));

  return newIngressPushStream;
}

HQSession::HQStreamTransportBase*
HQUpstreamSession::findPushStream(quic::StreamId streamId) {
  return findIngressPushStream(streamId);
}

HQUpstreamSession::HQIngressPushStream* FOLLY_NULLABLE
HQUpstreamSession::findIngressPushStream(quic::StreamId streamId) {
  auto lookup = streamLookup_.by<quic_stream_id>();
  auto res = lookup.find(streamId);
  if (res == lookup.end()) {
    return nullptr;
  } else {
    return findIngressPushStreamByPushId(res->get<push_id>());
  }
}

HQUpstreamSession::HQIngressPushStream* FOLLY_NULLABLE
HQUpstreamSession::findIngressPushStreamByPushId(hq::PushId pushId) {
  VLOG(4) << __func__ << " looking up ingress push stream by pushID=" << pushId;
  auto it = ingressPushStreams_.find(pushId);
  if (it == ingressPushStreams_.end()) {
    return nullptr;
  } else {
    return &it->second;
  }
}

bool
HQUpstreamSession::erasePushStream(
    quic::StreamId streamId) {
  auto lookup = streamLookup_.by<quic_stream_id>();
  auto rlookup = streamLookup_.by<push_id>();

  auto res = lookup.find(streamId);
  if (res != lookup.end()) {
    auto pushId = res->get<push_id>();
    // Ingress push stream may be using the push id
    // erase it as well if present
    ingressPushStreams_.erase(pushId);

    // Unconditionally erase the lookup entry table
    lookup.erase(res);
    CHECK(rlookup.find(pushId) == rlookup.end());
    return true;
  }
  return false;
}

bool
HQUpstreamSession::eraseStreamByPushId(hq::PushId pushId) {
  bool erased = ingressPushStreams_.erase(pushId);

  auto lookup = streamLookup_.by<push_id>();
  // Reverse lookup for the postconditions CHECK
  auto rlookup = streamLookup_.by<quic_stream_id>();

  auto res = lookup.find(pushId);
  if (res != lookup.end()) {
    auto streamId = res->get<quic_stream_id>();
    erased |= (lookup.erase(res) != lookup.end());
    // The corresponding stream id should not be present in
    // the reverse map
    CHECK(rlookup.find(streamId) == rlookup.end());
  }

  return erased;
}

uint32_t HQUpstreamSession::numberOfIngressPushStreams() const {
  return ingressPushStreams_.size();
}

void HQUpstreamSession::onNewPushStream(quic::StreamId pushStreamId,
                                        hq::PushId pushId,
                                        size_t toConsume) {
  VLOG(4) << __func__ << " streamID=" << pushStreamId << " pushId=" << pushId;

  DCHECK_GT(toConsume, 0);

  bool eom = false;
  if (serverPushLifecycleCb_) {
    serverPushLifecycleCb_->onNascentPushStreamBegin(pushStreamId, eom);
  }

  auto consumeRes = sock_->consume(pushStreamId, toConsume);
  CHECK(!consumeRes.hasError())
      << "Unexpected error " << consumeRes.error() << " while consuming "
      << toConsume << " bytes from stream=" << pushStreamId
      << " pushId=" << pushId;

  // Replace the peek callback with a read callback and pause the read callback
  sock_->setReadCallback(pushStreamId, this);
  sock_->setPeekCallback(pushStreamId, nullptr);
  sock_->pauseRead(pushStreamId);

  // Increment the sequence no to account for the new transport-like stream
  incrementSeqNo();

  streamLookup_.push_back(PushToStreamMap::value_type(pushId, pushStreamId));

  VLOG(4) << __func__ << " assigned lookup from pushID=" << pushId
          << " to streamID=" << pushStreamId;

  // We have successfully read the push id. Notify the testing callbacks
  if (serverPushLifecycleCb_) {
    serverPushLifecycleCb_->onNascentPushStream(pushStreamId, pushId, eom);
  }

  // If the transaction for the incoming push stream has been created
  // already, bind the new stream to the transaction
  auto ingressPushStream = findIngressPushStreamByPushId(pushId);

  if (ingressPushStream) {
    auto bound = tryBindIngressStreamToTxn(pushId, ingressPushStream);
    VLOG(4) << __func__ << " bound=" << bound << " pushID=" << pushId
            << " pushStreamID=" << pushStreamId << " to txn ";
  }
}

void HQUpstreamSession::HQIngressPushStream::bindTo(quic::StreamId streamId) {
  // Ensure the nascent push stream is in correct state
  // and that its push id matches this stream's push id
  DCHECK(txn_.getAssocTxnId().has_value());
  VLOG(4) << __func__ << " Binding streamID=" << streamId
          << " to txn=" << txn_.getID();
#if DEBUG
  // will throw bad-cast
  HQUpstreamSession& session = dynamic_cast<HQUpstreamSession&>(session_);
#else
  HQUpstreamSession& session = static_cast<HQUpstreamSession&>(session_);
#endif
  // Initialize this stream's codec with the id of the transport stream
  auto codec = session.versionUtils_->createCodec(streamId);
  initCodec(std::move(codec), __func__);
  DCHECK_EQ(*codecStreamId_, streamId);

  // Now that the codec is initialized, set the stream ID
  // of the push stream
  setIngressStreamId(streamId);
  DCHECK_EQ(getIngressStreamId(), streamId);

  // Enable ingress on this stream. Read callback for the stream's
  // id will be transferred to the HQSession
  initIngress(__func__);

  // Re-enable reads
  session.resumeReadsForPushStream(streamId);

  // Notify testing callbacks that a full push transaction
  // has been successfully initialized
  if (session.serverPushLifecycleCb_) {
    session.serverPushLifecycleCb_->onPushedTxn(&txn_,
                                                streamId,
                                                getPushId(),
                                                txn_.getAssocTxnId().value(),
                                                false /* eof */);
  }
}

void HQUpstreamSession::eraseUnboundStream(HQStreamTransportBase* hqStream) {
  auto hqPushIngressStream = dynamic_cast<HQIngressPushStream*>(hqStream);
  CHECK(hqPushIngressStream)
    << "Only HQIngressPushStream streams are allowed to be non-bound";
  eraseStreamByPushId(hqPushIngressStream->getPushId());
}

void HQUpstreamSession::cleanupUnboundPushStreams(
    std::vector<quic::StreamId> &streamsToCleanup) {
  // Find stream ids which have been added to the stream
  // lookup but lack matching HQIngressPushStream
  auto lookup = streamLookup_.by<push_id>();
  for (auto res : lookup) {
    auto streamId = res.get<quic_stream_id>();
    auto pushId = res.get<quic_stream_id>();
    if (!ingressPushStreams_.count(pushId)) {
      streamsToCleanup.push_back(streamId);
    }
  }
}
} // namespace proxygen
