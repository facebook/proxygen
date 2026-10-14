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

bool HQDownstreamSession::pushAllowedByGoaway(hq::PushId pushId) {
  // TODO: This is always true since we ignore client sent GOAWAY right now
  return pushId < peerMinUnseenId_;
}

HQDownstreamSession::HQEgressPushStream* FOLLY_NULLABLE
HQDownstreamSession::createEgressPushStream(hq::PushId pushId,
                                            quic::StreamId streamId,
                                            quic::StreamId parentStreamId) {

  VLOG(4) << __func__ << "sess=" << *this << " pushId=" << pushId
          << " isClosing()=" << isClosing() << " streamId=" << streamId
          << " parentStreamId=" << parentStreamId;

  // Use version utils to ensure that the session is not in draining state
  if (!pushAllowedByGoaway(pushId)) {
    VLOG(3) << __func__ << " Not creating - session is draining"
            << " sess=" << *this << " pushId=" << pushId
            << " isClosing()=" << isClosing() << " streamId=" << streamId
            << " parentStreamId=" << parentStreamId;
    return nullptr;
  }

  auto codec = createCodec(streamId);

  auto matchPair = egressPushStreams_.emplace(
      std::piecewise_construct,
      std::forward_as_tuple(streamId),
      std::forward_as_tuple(
          *this,
          streamId,
          pushId,
          parentStreamId,
          getNumTxnServed(),
          std::move(codec),
          WheelTimerInstance(transactionsTimeout_, getEventBase())));
  incrementSeqNo();

  pushIdToStreamId_[pushId] = streamId;
  streamIdToPushId_[streamId] = pushId;

  CHECK(matchPair.second) << "Emplacement failed, despite earlier "
                             "existence check.";

  // Generate the stream preface
  matchPair.first->second.generateStreamPreface();

  // Generate the push id
  matchPair.first->second.generateStreamPushId();

  // Notify pending egress on the stream
  matchPair.first->second.notifyPendingEgress();

  // tracks max historical streams
  HTTPSessionBase::onNewOutgoingStream(getNumOutgoingStreams());

  return &matchPair.first->second;
}

HQSession::HQStreamTransportBase* HQDownstreamSession::findPushStream(
    quic::StreamId streamId) {
  return findEgressPushStream(streamId);
}

HQDownstreamSession::HQEgressPushStream* FOLLY_NULLABLE
HQDownstreamSession::findEgressPushStream(quic::StreamId streamId) {
  auto it = egressPushStreams_.find(streamId);
  if (it == egressPushStreams_.end()) {
    return nullptr;
  } else {
    auto pstream = &it->second;
    DCHECK(pstream->isUsing(streamId));
    return pstream;
  }
}

bool HQDownstreamSession::erasePushStream(quic::StreamId streamId) {
  auto pushIdIter = streamIdToPushId_.find(streamId);
  if (pushIdIter != streamIdToPushId_.end()) {
    auto pushId = pushIdIter->second;
    pushIdToStreamId_.erase(pushId);
    streamIdToPushId_.erase(pushIdIter);
  }
  return egressPushStreams_.erase(streamId);
}

uint32_t HQDownstreamSession::numberOfEgressPushStreams() const {
  return egressPushStreams_.size();
}

// Push-stream implementation of the "sendPushPromise"
// It invokes HQStreamTransport::sendPushPromise
// Since this method does not uses codecs directly, it should not
// set an active codec
void HQDownstreamSession::HQEgressPushStream::sendPushPromise(
    HTTPTransaction* txn,
    folly::Optional<hq::PushId> pushId,
    const HTTPMessage& headers,
    HTTPHeaderSize* size,
    bool includeEOM) {

  CHECK(txn) << "Must be invoked on a live transaction";
  CHECK(txn->getAssocTxnId())
      << "Must be invoked on a transaction with a parent";
  CHECK_EQ(txn_.getID(), txn->getID()) << " Transaction stream mismatch";
  CHECK(pushId == folly::none) << " The push id is stored in the egress stream,"
                               << " and should not be passed by the session";

  auto parentStreamId = txn->getAssocTxnId();
#if DEBUG
  HQDownstreamSession& session = dynamic_cast<HQDownstreamSession&>(session_);
#else
  auto& session = static_cast<HQDownstreamSession&>(session_);
#endif
  auto parentStream = session.findNonDetachedStream(*parentStreamId);
  if (!parentStream) {
    session_.dropConnectionAsync(
        quic::QuicError(quic::TransportErrorCode::STREAM_STATE_ERROR,
                        "Send push promise on a stream without a parent"),
        kErrorConnection);
    return;
  }
  // Redirect to the parent transaction
  parentStream->sendPushPromise(txn, pushId_, headers, size, includeEOM);
}

size_t HQDownstreamSession::HQEgressPushStream::generateStreamPushId() {
  // reserve space for max quic interger len
  auto result = hq::writeStreamPreface(writeBuf_, pushId_);
  CHECK(result.has_value())
      << __func__ << " QUIC integer encoding error value=" << pushId_;

  return *result;
}

// Return a new push id that can be used for outgoing transactions
hq::PushId HQDownstreamSession::createNewPushId() {
  auto newPushId = nextAvailablePushId_++;
  return newPushId;
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
