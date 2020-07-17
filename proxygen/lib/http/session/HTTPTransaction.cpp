/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/session/HTTPTransaction.h>

#include <algorithm>
#include <folly/Conv.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/tracing/ScopedTraceSection.h>
#include <glog/logging.h>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/RFC2616.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>
#include <sstream>

using folly::IOBuf;
using std::unique_ptr;

namespace proxygen {

namespace {
const int64_t kApproximateMTU = 1400;
const std::chrono::seconds kRateLimitMaxDelay(10);
const uint64_t kMaxBufferPerTxn = 65536;
} // namespace

HTTPTransaction::HTTPTransaction(
    TransportDirection direction,
    HTTPCodec::StreamID id,
    uint32_t seqNo,
    Transport& transport,
    HTTP2PriorityQueueBase& egressQueue,
    folly::HHWheelTimer* timer,
    const folly::Optional<std::chrono::milliseconds>& defaultTimeout,
    HTTPSessionStats* stats,
    bool useFlowControl,
    uint32_t receiveInitialWindowSize,
    uint32_t sendInitialWindowSize,
    http2::PriorityUpdate priority,
    folly::Optional<HTTPCodec::StreamID> assocId,
    folly::Optional<HTTPCodec::ExAttributes> exAttributes)
    : deferredEgressBody_(folly::IOBufQueue::cacheChainLength()),
      direction_(direction),
      id_(id),
      seqNo_(seqNo),
      transport_(transport),
      stats_(stats),
      recvWindow_(receiveInitialWindowSize),
      sendWindow_(sendInitialWindowSize),
      egressQueue_(egressQueue),
      assocStreamId_(assocId),
      priority_(priority),
      ingressPaused_(false),
      egressPaused_(false),
      flowControlPaused_(false),
      handlerEgressPaused_(false),
      egressRateLimited_(false),
      useFlowControl_(useFlowControl),
      aborted_(false),
      deleting_(false),
      firstByteSent_(false),
      firstHeaderByteSent_(false),
      inResume_(false),
      isCountedTowardsStreamLimit_(false),
      ingressErrorSeen_(false),
      priorityFallback_(false),
      headRequest_(false),
      enableLastByteFlushedTracking_(false),
      enableBodyLastByteDeliveryTracking_(false),
      transactionTimeout_(defaultTimeout),
      timer_(timer) {

  if (assocStreamId_) {
    if (isUpstream()) {
      egressState_ = HTTPTransactionEgressSM::State::SendingDone;
    } else {
      ingressState_ = HTTPTransactionIngressSM::State::ReceivingDone;
    }
  }

  if (exAttributes) {
    exAttributes_ = exAttributes;
    if (exAttributes_->unidirectional) {
      if (isRemoteInitiated()) {
        egressState_ = HTTPTransactionEgressSM::State::SendingDone;
      } else {
        ingressState_ = HTTPTransactionIngressSM::State::ReceivingDone;
      }
    }
  }

  refreshTimeout();
  if (stats_) {
    stats_->recordTransactionOpened();
  }

  queueHandle_ =
      egressQueue_.addTransaction(id_, priority, this, false, &insertDepth_);
  if (priority.streamDependency != egressQueue_.getRootId() &&
      insertDepth_ == 1) {
    priorityFallback_ = true;
  }

  currentDepth_ = insertDepth_;
}

void HTTPTransaction::onDelayedDestroy(bool delayed) {
  if (!isEgressComplete() || !isIngressComplete() || isEnqueued() ||
      pendingByteEvents_ > 0 || deleting_) {
    return;
  }
  VLOG(4) << "destroying transaction " << *this;
  deleting_ = true;
  if (handler_) {
    handler_->detachTransaction();
    handler_ = nullptr;
  }
  transportCallback_ = nullptr;
  const auto bytesBuffered = recvWindow_.getOutstanding();
  if (bytesBuffered) {
    transport_.notifyIngressBodyProcessed(bytesBuffered);
  }
  transport_.detach(this);
  (void)delayed; // prevent unused variable warnings
}

HTTPTransaction::~HTTPTransaction() {
  // Cancel transaction timeout if still scheduled.
  if (isScheduled()) {
    cancelTimeout();
  }

  if (stats_) {
    stats_->recordTransactionClosed();
  }
  if (isEnqueued()) {
    dequeue();
  }
  // TODO: handle the case where the priority node hangs out longer than
  // the transaction
  egressQueue_.removeTransaction(queueHandle_);
}

void HTTPTransaction::reset(bool useFlowControl,
                            uint32_t receiveInitialWindowSize,
                            uint32_t receiveStreamWindowSize,
                            uint32_t sendInitialWindowSize) {
  useFlowControl_ = useFlowControl;
  recvWindow_.setCapacity(receiveInitialWindowSize);
  setReceiveWindow(receiveStreamWindowSize);
  sendWindow_.setCapacity(sendInitialWindowSize);
}

void HTTPTransaction::onIngressHeadersComplete(
    std::unique_ptr<HTTPMessage> msg) {
  DestructorGuard g(this);
  msg->setSeqNo(seqNo_);
  if (isUpstream() && !isPushed() && msg->isResponse()) {
    lastResponseStatus_ = msg->getStatusCode();
  }
  if (!validateIngressStateTransition(
          HTTPTransactionIngressSM::Event::onHeaders)) {
    return;
  }
  if (msg->isRequest()) {
    headRequest_ = (msg->getMethod() == HTTPMethod::HEAD);
  }

  if ((msg->isRequest() && msg->getMethod() != HTTPMethod::CONNECT) ||
      (msg->isResponse() && !headRequest_ &&
       !RFC2616::responseBodyMustBeEmpty(msg->getStatusCode()))) {
    // CONNECT payload has no defined semantics
    const auto& contentLen =
        msg->getHeaders().getSingleOrEmpty(HTTP_HEADER_CONTENT_LENGTH);
    if (!contentLen.empty()) {
      try {
        expectedIngressContentLengthRemaining_ =
            folly::to<uint64_t>(contentLen);
      } catch (const folly::ConversionError& ex) {
        LOG(ERROR) << "Invalid content-length: " << contentLen
                   << ", ex=" << ex.what() << " " << *this;
      }
      if (expectedIngressContentLengthRemaining_) {
        expectedIngressContentLength_ =
            expectedIngressContentLengthRemaining_.value();
      }
    }
  }
  if (transportCallback_) {
    transportCallback_->headerBytesReceived(msg->getIngressHeaderSize());
  }
  updateIngressCompressionInfo(transport_.getCodec().getCompressionInfo());
  if (mustQueueIngress()) {
    checkCreateDeferredIngress();
    deferredIngress_->emplace(
        id_, HTTPEvent::Type::HEADERS_COMPLETE, std::move(msg));
    VLOG(4) << "Queued ingress event of type "
            << HTTPEvent::Type::HEADERS_COMPLETE << " " << *this;
  } else {
    processIngressHeadersComplete(std::move(msg));
  }
}

void HTTPTransaction::processIngressHeadersComplete(
    std::unique_ptr<HTTPMessage> msg) {
  DestructorGuard g(this);
  if (aborted_) {
    return;
  }
  refreshTimeout();
  if (handler_ && !isIngressComplete()) {
    handler_->onHeadersComplete(std::move(msg));
  }
}

bool HTTPTransaction::updateContentLengthRemaining(size_t len) {
  if (expectedIngressContentLengthRemaining_.has_value()) {
    if (expectedIngressContentLengthRemaining_.value() >= len) {
      expectedIngressContentLengthRemaining_ =
          expectedIngressContentLengthRemaining_.value() - len;
    } else {
      auto errorMsg = folly::to<std::string>(
          "Content-Length/body mismatch: received=",
          len,
          " expecting no more than ",
          expectedIngressContentLengthRemaining_.value());
      LOG(ERROR) << errorMsg << " " << *this;
      if (handler_) {
        HTTPException ex(HTTPException::Direction::INGRESS, errorMsg);
        ex.setProxygenError(kErrorParseBody);
        onError(ex);
      }
      return false;
    }
  }
  return true;
}

void HTTPTransaction::onIngressBody(unique_ptr<IOBuf> chain, uint16_t padding) {
  FOLLY_SCOPED_TRACE_SECTION("HTTPTransaction - onIngressBody");
  DestructorGuard g(this);
  if (isIngressEOMSeen()) {
    sendAbort(ErrorCode::STREAM_CLOSED);
    return;
  }
  auto len = chain->computeChainDataLength();
  if (len == 0) {
    return;
  }
  if (!validateIngressStateTransition(
          HTTPTransactionIngressSM::Event::onBody)) {
    return;
  }
  if (!updateContentLengthRemaining(len)) {
    return;
  }

  if (transportCallback_) {
    transportCallback_->bodyBytesReceived(len);
  }
  // register the bytes in the receive window
  if (!recvWindow_.reserve(len + padding, useFlowControl_)) {
    LOG(ERROR)
        << "recvWindow_.reserve failed with len=" << len
        << " padding=" << padding << " capacity=" << recvWindow_.getCapacity()
        << " outstanding=" << recvWindow_.getOutstanding() << " " << *this;
    sendAbort(ErrorCode::FLOW_CONTROL_ERROR);
    return;
  } else {
    CHECK(recvWindow_.free(padding));
    recvToAck_ += padding;
  }
  if (mustQueueIngress()) {
    checkCreateDeferredIngress();
    deferredIngress_->emplace(id_, HTTPEvent::Type::BODY, std::move(chain));
    VLOG(4) << "Queued ingress event of type " << HTTPEvent::Type::BODY
            << " size=" << len << " " << *this;
  } else {
    CHECK(recvWindow_.free(len));
    processIngressBody(std::move(chain), len);
  }
}

void HTTPTransaction::processIngressBody(unique_ptr<IOBuf> chain, size_t len) {
  FOLLY_SCOPED_TRACE_SECTION("HTTPTransaction - processIngressBody");
  DestructorGuard g(this);
  if (aborted_) {
    return;
  }
  refreshTimeout();
  transport_.notifyIngressBodyProcessed(len);
  auto chainLen = chain->computeChainDataLength();
  if (handler_) {
    if (!isIngressComplete()) {
      handler_->onBodyWithOffset(ingressBodyOffset_, std::move(chain));
    }

    if (useFlowControl_ && !isIngressEOMSeen()) {
      recvToAck_ += len;
      if (recvToAck_ > 0) {
        uint32_t divisor = 2;
        if (transport_.isDraining()) {
          // only send window updates for draining transports when window is
          // closed
          divisor = 1;
        }
        if (uint32_t(recvToAck_) >= (recvWindow_.getCapacity() / divisor)) {
          flushWindowUpdate();
        }
      }
    } // else don't care about window updates
  }
  ingressBodyOffset_ += chainLen;
}

void HTTPTransaction::onIngressChunkHeader(size_t length) {
  if (!validateIngressStateTransition(
          HTTPTransactionIngressSM::Event::onChunkHeader)) {
    return;
  }
  if (mustQueueIngress()) {
    checkCreateDeferredIngress();
    deferredIngress_->emplace(id_, HTTPEvent::Type::CHUNK_HEADER, length);
    VLOG(4) << "Queued ingress event of type " << HTTPEvent::Type::CHUNK_HEADER
            << " size=" << length << " " << *this;
  } else {
    processIngressChunkHeader(length);
  }
}

void HTTPTransaction::processIngressChunkHeader(size_t length) {
  DestructorGuard g(this);
  if (aborted_) {
    return;
  }
  refreshTimeout();
  if (handler_ && !isIngressComplete()) {
    handler_->onChunkHeader(length);
  }
}

void HTTPTransaction::onIngressChunkComplete() {
  if (!validateIngressStateTransition(
          HTTPTransactionIngressSM::Event::onChunkComplete)) {
    return;
  }
  if (mustQueueIngress()) {
    checkCreateDeferredIngress();
    deferredIngress_->emplace(id_, HTTPEvent::Type::CHUNK_COMPLETE);
    VLOG(4) << "Queued ingress event of type "
            << HTTPEvent::Type::CHUNK_COMPLETE << " " << *this;
  } else {
    processIngressChunkComplete();
  }
}

void HTTPTransaction::processIngressChunkComplete() {
  DestructorGuard g(this);
  if (aborted_) {
    return;
  }
  refreshTimeout();
  if (handler_ && !isIngressComplete()) {
    handler_->onChunkComplete();
  }
}

void HTTPTransaction::onIngressTrailers(unique_ptr<HTTPHeaders> trailers) {
  if (!validateIngressStateTransition(
          HTTPTransactionIngressSM::Event::onTrailers)) {
    return;
  }
  if (mustQueueIngress()) {
    checkCreateDeferredIngress();
    deferredIngress_->emplace(
        id_, HTTPEvent::Type::TRAILERS_COMPLETE, std::move(trailers));
    VLOG(4) << "Queued ingress event of type "
            << HTTPEvent::Type::TRAILERS_COMPLETE << " " << *this;
  } else {
    processIngressTrailers(std::move(trailers));
  }
}

void HTTPTransaction::processIngressTrailers(unique_ptr<HTTPHeaders> trailers) {
  DestructorGuard g(this);
  if (aborted_) {
    return;
  }
  refreshTimeout();
  if (handler_ && !isIngressComplete()) {
    handler_->onTrailers(std::move(trailers));
  }
}

void HTTPTransaction::onIngressUpgrade(UpgradeProtocol protocol) {
  if (!validateIngressStateTransition(
          HTTPTransactionIngressSM::Event::onUpgrade)) {
    return;
  }
  if (mustQueueIngress()) {
    checkCreateDeferredIngress();
    deferredIngress_->emplace(id_, HTTPEvent::Type::UPGRADE, protocol);
    VLOG(4) << "Queued ingress event of type " << HTTPEvent::Type::UPGRADE
            << " " << *this;
  } else {
    processIngressUpgrade(protocol);
  }
}

void HTTPTransaction::processIngressUpgrade(UpgradeProtocol protocol) {
  DestructorGuard g(this);
  if (aborted_) {
    return;
  }
  if (handler_ && !isIngressComplete()) {
    handler_->onUpgrade(protocol);
  }
}

void HTTPTransaction::onIngressEOM() {
  if (isIngressEOMSeen()) {
    // This can happen when HTTPSession calls onIngressEOF()
    sendAbort(ErrorCode::STREAM_CLOSED);
    return;
  }
  if (expectedIngressContentLengthRemaining_.has_value() &&
      expectedIngressContentLengthRemaining_.value() > 0) {
    auto errorMsg = folly::to<std::string>(
        "Content-Length/body mismatch: expecting another ",
        expectedIngressContentLengthRemaining_.value());
    LOG(ERROR) << errorMsg << " " << *this;
    if (handler_) {
      HTTPException ex(HTTPException::Direction::INGRESS, errorMsg);
      ex.setProxygenError(kErrorParseBody);
      onError(ex);
    }
    return;
  }

  // TODO: change the codec to not give an EOM callback after a 100 response?
  // We could then delete the below 'if'
  if (isUpstream() && extraResponseExpected()) {
    VLOG(4) << "Ignoring EOM on initial 100 response on " << *this;
    return;
  }
  if (!validateIngressStateTransition(HTTPTransactionIngressSM::Event::onEOM)) {
    return;
  }
  // We need to update the read timeout here.  We're not likely to be
  // expecting any more ingress, and the timer should be cancelled
  // immediately.  If we are expecting more, this will reset the timer.
  updateReadTimeout();
  if (mustQueueIngress()) {
    checkCreateDeferredIngress();
    deferredIngress_->emplace(id_, HTTPEvent::Type::MESSAGE_COMPLETE);
    VLOG(4) << "Queued ingress event of type "
            << HTTPEvent::Type::MESSAGE_COMPLETE << " " << *this;
  } else {
    processIngressEOM();
  }
}

void HTTPTransaction::processIngressEOM() {
  DestructorGuard g(this);
  if (aborted_) {
    return;
  }
  VLOG(4) << "ingress EOM on " << *this;
  const bool wasComplete = isIngressComplete();
  if (!validateIngressStateTransition(
          HTTPTransactionIngressSM::Event::eomFlushed)) {
    return;
  }
  if (handler_) {
    if (!wasComplete) {
      handler_->onEOM();
    }
  } else {
    markEgressComplete();
  }
  updateReadTimeout();
}

bool HTTPTransaction::isExpectingWindowUpdate() const {
  return egressState_ != HTTPTransactionEgressSM::State::SendingDone &&
         useFlowControl_ && sendWindow_.getSize() <= 0;
}

bool HTTPTransaction::isExpectingIngress() const {
  return isExpectingWindowUpdate() || (!ingressPaused_ && !isIngressEOMSeen());
}

void HTTPTransaction::updateReadTimeout() {
  if (isExpectingIngress()) {
    refreshTimeout();
  } else {
    cancelTimeout();
  }
}

void HTTPTransaction::markIngressComplete() {
  VLOG(4) << "Marking ingress complete on " << *this;
  ingressState_ = HTTPTransactionIngressSM::State::ReceivingDone;
  deferredIngress_.reset();
  cancelTimeout();
}

void HTTPTransaction::markEgressComplete() {
  VLOG(4) << "Marking egress complete on " << *this;
  if (deferredEgressBody_.chainLength() && isEnqueued()) {
    int64_t deferredEgressBodyBytes =
        folly::to<int64_t>(deferredEgressBody_.chainLength());
    transport_.notifyEgressBodyBuffered(-deferredEgressBodyBytes);
  }
  deferredEgressBody_.move();
  if (isEnqueued()) {
    dequeue();
  }
  egressState_ = HTTPTransactionEgressSM::State::SendingDone;
}

bool HTTPTransaction::validateIngressStateTransition(
    HTTPTransactionIngressSM::Event event) {
  DestructorGuard g(this);

  if (!HTTPTransactionIngressSM::transit(ingressState_, event)) {
    std::stringstream ss;
    ss << "Invalid ingress state transition, state=" << ingressState_
       << ", event=" << event << ", streamID=" << id_;
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS, ss.str());
    ex.setProxygenError(kErrorIngressStateTransition);
    ex.setCodecStatusCode(ErrorCode::INTERNAL_ERROR);
    // This will invoke sendAbort() and also inform the handler of the
    // error and detach the handler.
    onError(ex);
    return false;
  }
  return true;
}

void HTTPTransaction::onError(const HTTPException& error) {
  DestructorGuard g(this);

  const bool wasAborted = aborted_; // see comment below
  const bool wasEgressComplete = isEgressComplete();
  const bool wasIngressComplete = isIngressComplete();
  bool notify = (handler_);
  HTTPException::Direction direction = error.getDirection();

  if (direction == HTTPException::Direction::INGRESS && isIngressEOMSeen() &&
      isExpectingWindowUpdate()) {
    // we got an ingress error, we've seen the entire message, but we're
    // expecting more (window updates).  These aren't coming, convert to
    // INGRESS_AND_EGRESS
    VLOG(4) << "Converting ingress error to ingress+egress due to"
               " flow control, and aborting "
            << *this;
    direction = HTTPException::Direction::INGRESS_AND_EGRESS;
    sendAbort(ErrorCode::FLOW_CONTROL_ERROR);
  }

  if (error.getProxygenError() == kErrorStreamAbort) {
    DCHECK(error.getDirection() ==
           HTTPException::Direction::INGRESS_AND_EGRESS);
    aborted_ = true;
  } else if (error.hasCodecStatusCode()) {
    DCHECK(error.getDirection() ==
           HTTPException::Direction::INGRESS_AND_EGRESS);
    sendAbort(error.getCodecStatusCode());
  }

  switch (direction) {
    case HTTPException::Direction::INGRESS_AND_EGRESS:
      markEgressComplete();
      markIngressComplete();
      if (wasEgressComplete && wasIngressComplete &&
          // We mark egress complete before we get acknowledgement of the
          // write segment finishing successfully.
          // TODO: instead of using DestructorGuard hacks to keep txn around,
          // use an explicit callback function and set egress complete after
          // last byte flushes (or egress error occurs), see #3912823
          (error.getProxygenError() != kErrorWriteTimeout || wasAborted)) {
        notify = false;
      }
      break;
    case HTTPException::Direction::EGRESS:
      markEgressComplete();
      if (!wasEgressComplete && isIngressEOMSeen() && ingressErrorSeen_) {
        // we've already seen an ingress error but we ignored it, hoping the
        // handler would resume and read our queued EOM.  Now both sides are
        // dead and we need to kill this transaction.
        markIngressComplete();
      }
      if (wasEgressComplete &&
          !shouldNotifyExTxnError(HTTPException::Direction::EGRESS)) {
        notify = false;
      }
      break;
    case HTTPException::Direction::INGRESS:
      if (isIngressEOMSeen() &&
          !shouldNotifyExTxnError(HTTPException::Direction::INGRESS)) {
        // Not an error, for now
        ingressErrorSeen_ = true;
        return;
      }
      markIngressComplete();
      if (wasIngressComplete &&
          !shouldNotifyExTxnError(HTTPException::Direction::INGRESS)) {
        notify = false;
      }
      break;
  }
  if (notify && handler_) {
    // mark egress complete may result in handler detaching
    handler_->onError(error);
  }
}

void HTTPTransaction::onGoaway(ErrorCode code) {
  DestructorGuard g(this);
  VLOG(4) << "received GOAWAY notification on " << *this;
  // This callback can be received at any time and does not affect this
  // transaction's ingress or egress state machines. If it would have
  // affected this transaction's state, we would have received onError()
  // instead.
  if (handler_) {
    handler_->onGoaway(code);
  }
}

void HTTPTransaction::onIngressTimeout() {
  DestructorGuard g(this);
  VLOG(4) << "ingress timeout on " << *this;
  pauseIngress();
  bool windowUpdateTimeout = !isEgressComplete() && isExpectingWindowUpdate();
  if (handler_) {
    if (windowUpdateTimeout) {
      HTTPException ex(
          HTTPException::Direction::INGRESS_AND_EGRESS,
          folly::to<std::string>("ingress timeout, streamID=", id_));
      ex.setProxygenError(kErrorWriteTimeout);
      // This is a protocol error
      ex.setCodecStatusCode(ErrorCode::PROTOCOL_ERROR);
      onError(ex);
    } else {
      HTTPException ex(
          HTTPException::Direction::INGRESS,
          folly::to<std::string>("ingress timeout, streamID=", id_));
      ex.setProxygenError(kErrorTimeout);
      onError(ex);
    }
  } else {
    markIngressComplete();
    markEgressComplete();
  }
}

void HTTPTransaction::onIngressWindowUpdate(const uint32_t amount) {
  if (!useFlowControl_) {
    return;
  }
  DestructorGuard g(this);
  VLOG(4) << "Remote side ack'd " << amount << " bytes " << *this;
  updateReadTimeout();
  if (sendWindow_.free(amount)) {
    notifyTransportPendingEgress();
  } else {
    LOG(ERROR) << "sendWindow_.free failed with amount=" << amount
               << " capacity=" << sendWindow_.getCapacity()
               << " outstanding=" << sendWindow_.getOutstanding() << " "
               << *this;
    sendAbort(ErrorCode::FLOW_CONTROL_ERROR);
  }
}

void HTTPTransaction::onIngressSetSendWindow(const uint32_t newWindowSize) {
  if (!useFlowControl_) {
    return;
  }
  updateReadTimeout();
  if (sendWindow_.setCapacity(newWindowSize)) {
    notifyTransportPendingEgress();
  } else {
    LOG(ERROR)
        << "sendWindow_.setCapacity failed with newWindowSize=" << newWindowSize
        << " capacity=" << sendWindow_.getCapacity()
        << " outstanding=" << sendWindow_.getOutstanding() << " " << *this;
    sendAbort(ErrorCode::FLOW_CONTROL_ERROR);
  }
}

void HTTPTransaction::onEgressTimeout() {
  DestructorGuard g(this);
  VLOG(4) << "egress timeout on " << *this;
  if (handler_) {
    HTTPException ex(HTTPException::Direction::EGRESS,
                     folly::to<std::string>("egress timeout, streamID=", id_));
    ex.setProxygenError(kErrorTimeout);
    onError(ex);
  } else {
    markEgressComplete();
  }
}

void HTTPTransaction::onEgressHeaderFirstByte() {
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->firstHeaderByteFlushed();
  }
}

void HTTPTransaction::onEgressBodyFirstByte() {
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->firstByteFlushed();
  }
}

void HTTPTransaction::onEgressBodyLastByte() {
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->lastByteFlushed();
  }
}

void HTTPTransaction::onEgressTrackedByte() {
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->trackedByteFlushed();
  }
}

void HTTPTransaction::onEgressLastByteAck(std::chrono::milliseconds latency) {
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->lastByteAcked(latency);
  }
}

void HTTPTransaction::onLastEgressHeaderByteAcked() {
  FOLLY_SCOPED_TRACE_SECTION("HTTPTransaction - onLastEgressHeaderByteAcked");
  egressHeadersDelivered_ = true;
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->lastEgressHeaderByteAcked();
  }
}

void HTTPTransaction::onEgressBodyBytesAcked(uint64_t bodyOffset) {
  FOLLY_SCOPED_TRACE_SECTION("HTTPTransaction - onEgressBodyBytesAcked");
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->bodyBytesDelivered(bodyOffset);
  }
}

void HTTPTransaction::onEgressBodyDeliveryCanceled(uint64_t bodyOffset) {
  FOLLY_SCOPED_TRACE_SECTION("HTTPTransaction - onEgressBodyDeliveryCanceled");
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->bodyBytesDeliveryCancelled(bodyOffset);
  }
}

void HTTPTransaction::onEgressTrackedByteEventTX(const ByteEvent& event) {
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->trackedByteEventTX(event);
  }
}

void HTTPTransaction::onEgressTrackedByteEventAck(const ByteEvent& event) {
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->trackedByteEventAck(event);
  }
}

void HTTPTransaction::onEgressTransportAppRateLimited() {
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->transportAppRateLimited();
  }
}

void HTTPTransaction::onIngressBodyPeek(uint64_t bodyOffset,
                                        const folly::IOBuf& chain) {
  FOLLY_SCOPED_TRACE_SECTION("HTTPTransaction - onIngressBodyPeek");
  DestructorGuard g(this);
  if (handler_) {
    handler_->onBodyPeek(bodyOffset, chain);
  }
}

void HTTPTransaction::onIngressBodySkipped(uint64_t nextBodyOffset) {
  FOLLY_SCOPED_TRACE_SECTION("HTTPTransaction - onIngressBodySkipped");
  CHECK_LE(ingressBodyOffset_, nextBodyOffset);

  uint64_t skipLen = nextBodyOffset - ingressBodyOffset_;
  if (!updateContentLengthRemaining(skipLen)) {
    return;
  }
  ingressBodyOffset_ = nextBodyOffset;

  DestructorGuard g(this);
  if (handler_) {
    handler_->onBodySkipped(nextBodyOffset);
  }
}

void HTTPTransaction::onIngressBodyRejected(uint64_t nextBodyOffset) {
  FOLLY_SCOPED_TRACE_SECTION("HTTPTransaction - onIngressBodyRejected");
  DestructorGuard g(this);
  if (nextBodyOffset <= *actualResponseLength_) {
    return;
  }
  actualResponseLength_ = nextBodyOffset;

  if (handler_) {
    handler_->onBodyRejected(nextBodyOffset);
  }
}

void HTTPTransaction::sendHeadersWithOptionalEOM(const HTTPMessage& headers,
                                                 bool eom) {
  CHECK(HTTPTransactionEgressSM::transit(
      egressState_, HTTPTransactionEgressSM::Event::sendHeaders));
  DCHECK(!isEgressComplete());
  if (!headers.isRequest() && !isPushed()) {
    lastResponseStatus_ = headers.getStatusCode();
  }
  if (headers.isRequest()) {
    headRequest_ = (headers.getMethod() == HTTPMethod::HEAD);
  }

  if (headers.isResponse() && !headRequest_) {
    const auto& contentLen =
        headers.getHeaders().getSingleOrEmpty(HTTP_HEADER_CONTENT_LENGTH);
    if (!contentLen.empty()) {
      try {
        expectedResponseLength_ = folly::to<uint64_t>(contentLen);
      } catch (const folly::ConversionError& ex) {
        LOG(ERROR) << "Invalid content-length: " << contentLen
                   << ", ex=" << ex.what() << " " << *this;
      }
    }
  }
  HTTPHeaderSize size;
  transport_.sendHeaders(this, headers, &size, eom);
  if (transportCallback_) {
    transportCallback_->headerBytesGenerated(size);
  }
  updateEgressCompressionInfo(transport_.getCodec().getCompressionInfo());
  if (eom) {
    CHECK(HTTPTransactionEgressSM::transit(
        egressState_, HTTPTransactionEgressSM::Event::sendEOM));
    // trailers are supported in this case:
    // trailers are for chunked encoding-transfer of a body
    if (transportCallback_) {
      transportCallback_->bodyBytesGenerated(0);
    }
    CHECK(HTTPTransactionEgressSM::transit(
        egressState_, HTTPTransactionEgressSM::Event::eomFlushed));
  }
  flushWindowUpdate();
}

void HTTPTransaction::sendHeadersWithEOM(const HTTPMessage& header) {
  sendHeadersWithOptionalEOM(header, true);
}

void HTTPTransaction::sendHeaders(const HTTPMessage& header) {
  partiallyReliable_ = partiallyReliable_ || header.isPartiallyReliable();
  sendHeadersWithOptionalEOM(header, false);
}

void HTTPTransaction::sendBody(std::unique_ptr<folly::IOBuf> body) {
  DestructorGuard guard(this);
  bool chunking =
      ((egressState_ == HTTPTransactionEgressSM::State::ChunkHeaderSent) &&
       !transport_.getCodec().supportsParallelRequests()); // see
                                                           // sendChunkHeader

  CHECK(HTTPTransactionEgressSM::transit(
      egressState_, HTTPTransactionEgressSM::Event::sendBody));

  if (body) {
    size_t bodyLen = body->computeChainDataLength();
    actualResponseLength_ = actualResponseLength_.value() + bodyLen;

    if (chunking) {
      // Note, this check doesn't account for cases where sendBody is called
      // multiple times for a single chunk, and the total length exceeds the
      // header.
      DCHECK(!chunkHeaders_.empty());
      DCHECK_LE(bodyLen, chunkHeaders_.back().length)
          << "Sent body longer than chunk header ";
    }
    deferredEgressBody_.append(std::move(body));
    if (*actualResponseLength_ && enableBodyLastByteDeliveryTracking_) {
      transport_.trackEgressBodyDelivery(*actualResponseLength_);
    }
    if (isEnqueued()) {
      transport_.notifyEgressBodyBuffered(bodyLen);
    }
  }
  notifyTransportPendingEgress();
}

bool HTTPTransaction::onWriteReady(const uint32_t maxEgress, double ratio) {
  DestructorGuard g(this);
  DCHECK(isEnqueued());
  if (prioritySample_) {
    updateRelativeWeight(ratio);
  }
  cumulativeRatio_ += ratio;
  egressCalls_++;
  sendDeferredBody(maxEgress);
  return isEnqueued();
}

// Send up to maxEgress body bytes, including pendingEOM if appropriate
size_t HTTPTransaction::sendDeferredBody(const uint32_t maxEgress) {
  const int32_t windowAvailable = sendWindow_.getSize();
  const uint32_t sendWindow =
      useFlowControl_
          ? std::min<uint32_t>(maxEgress,
                               windowAvailable > 0 ? windowAvailable : 0)
          : maxEgress;

  // We shouldn't be called if we have no pending body/EOM, egress is paused, or
  // the send window is closed
  CHECK((deferredEgressBody_.chainLength() > 0 || isEgressEOMQueued()) &&
        sendWindow > 0);

  const size_t bytesLeft = deferredEgressBody_.chainLength();

  size_t canSend = std::min<size_t>(sendWindow, bytesLeft);

  if (maybeDelayForRateLimit()) {
    // Timeout will call notifyTransportPendingEgress again
    return 0;
  }

  size_t curLen = 0;
  size_t nbytes = 0;
  bool willSendEOM = false;

  if (chunkHeaders_.empty()) {
    curLen = canSend;
    std::unique_ptr<IOBuf> body = deferredEgressBody_.split(curLen);
    willSendEOM = hasPendingEOM();
    DCHECK(curLen > 0 || willSendEOM);
    if (curLen > 0) {
      if (willSendEOM) {
        // we have to dequeue BEFORE sending the EOM =(
        dequeue();
      }
      nbytes = sendBodyNow(std::move(body), curLen, willSendEOM);
      willSendEOM = false;
    } // else we got called with only a pending EOM, handled below
  } else {
    // This body is expliticly chunked
    CHECK(!partiallyReliable_)
        << __func__ << ": chunking not supported in partially reliable mode.";
    while (!chunkHeaders_.empty() && canSend > 0) {
      Chunk& chunk = chunkHeaders_.front();
      if (!chunk.headerSent) {
        nbytes += transport_.sendChunkHeader(this, chunk.length);
        chunk.headerSent = true;
      }
      curLen = std::min<size_t>(chunk.length, canSend);
      std::unique_ptr<folly::IOBuf> cur = deferredEgressBody_.split(curLen);
      VLOG(4) << "sending " << curLen << " fin=false";
      nbytes += sendBodyNow(std::move(cur), curLen, false);
      canSend -= curLen;
      chunk.length -= curLen;
      if (chunk.length == 0) {
        nbytes += transport_.sendChunkTerminator(this);
        chunkHeaders_.pop_front();
      } else {
        DCHECK_EQ(canSend, 0);
      }
    }
    willSendEOM = hasPendingEOM();
  }
  // Send any queued eom
  if (willSendEOM) {
    nbytes += sendEOMNow();
  }

  // Update the handler's pause state
  notifyTransportPendingEgress();

  if (transportCallback_) {
    transportCallback_->bodyBytesGenerated(nbytes);
  }
  return nbytes;
}

bool HTTPTransaction::maybeDelayForRateLimit() {
  if (egressLimitBytesPerMs_ <= 0) {
    // No rate limiting
    return false;
  }

  if (numLimitedBytesEgressed_ == 0) {
    // If we haven't egressed any bytes yet, don't delay.
    return false;
  }

  int64_t limitedDurationMs =
      (int64_t)millisecondsBetween(getCurrentTime(), startRateLimit_).count();

  // Algebra!  Try to figure out the next time send where we'll
  // be allowed to send at least 1 full packet's worth.  The
  // formula we're using is:
  //   (bytesSoFar + packetSize) / (timeSoFar + delay) == targetRateLimit
  std::chrono::milliseconds requiredDelay(
      (((int64_t)numLimitedBytesEgressed_ + kApproximateMTU) -
       ((int64_t)egressLimitBytesPerMs_ * limitedDurationMs)) /
      (int64_t)egressLimitBytesPerMs_);

  if (requiredDelay.count() <= 0) {
    // No delay required
    return false;
  }

  if (requiredDelay > kRateLimitMaxDelay) {
    // The delay should never be this long
    VLOG(4) << "ratelim: Required delay too long (" << requiredDelay.count()
            << "ms), ignoring";
    return false;
  }

  // Delay required

  egressRateLimited_ = true;

  if (timer_) {
    timer_->scheduleTimeout(&rateLimitCallback_, requiredDelay);
  }

  notifyTransportPendingEgress();
  return true;
}

void HTTPTransaction::rateLimitTimeoutExpired() {
  egressRateLimited_ = false;
  notifyTransportPendingEgress();
}

size_t HTTPTransaction::sendEOMNow() {
  VLOG(4) << "egress EOM on " << *this;
  // TODO: with ByteEvent refactor, we will have to delay changing this
  // state until later
  CHECK(HTTPTransactionEgressSM::transit(
      egressState_, HTTPTransactionEgressSM::Event::eomFlushed));
  size_t nbytes = transport_.sendEOM(this, trailers_.get());
  trailers_.reset();
  return nbytes;
}

size_t HTTPTransaction::sendBodyNow(std::unique_ptr<folly::IOBuf> body,
                                    size_t bodyLen,
                                    bool sendEom) {
  static const std::string noneStr = "None";
  DCHECK(body);
  DCHECK_GT(bodyLen, 0);
  size_t nbytes = 0;
  if (useFlowControl_) {
    CHECK(sendWindow_.reserve(bodyLen));
  }
  VLOG(4) << "Sending " << bodyLen
          << " bytes of body. eom=" << ((sendEom) ? "yes" : "no")
          << " send_window is "
          << (useFlowControl_
                  ? folly::to<std::string>(
                        sendWindow_.getSize(), " / ", sendWindow_.getCapacity())
                  : noneStr)
          << " trailers=" << ((trailers_) ? "yes" : "no") << " " << *this;
  DCHECK_LT(bodyLen, std::numeric_limits<int64_t>::max());
  transport_.notifyEgressBodyBuffered(-static_cast<int64_t>(bodyLen));
  if (sendEom && !trailers_) {
    CHECK(HTTPTransactionEgressSM::transit(
        egressState_, HTTPTransactionEgressSM::Event::eomFlushed));
  } else if (ingressErrorSeen_ && isExpectingWindowUpdate()) {
    // I don't know how we got here but we're in trouble.  We need a window
    // update to continue but we've already seen an ingress error.
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                     folly::to<std::string>("window blocked with ingress error,"
                                            " streamID=",
                                            id_));
    ex.setProxygenError(kErrorEOF);
    ex.setCodecStatusCode(ErrorCode::FLOW_CONTROL_ERROR);
    onError(ex);
    return 0;
  }
  updateReadTimeout();
  egressBodyBytesCommittedToTransport_ += body->computeChainDataLength();
  nbytes = transport_.sendBody(this,
                               std::move(body),
                               sendEom && !trailers_,
                               enableLastByteFlushedTracking_);
  if (sendEom && trailers_) {
    sendEOMNow();
  }
  if (isPrioritySampled()) {
    updateTransactionBytesSent(bodyLen);
  }
  if (egressLimitBytesPerMs_ > 0) {
    numLimitedBytesEgressed_ += nbytes;
  }
  return nbytes;
}

void HTTPTransaction::sendEOM() {
  DestructorGuard g(this);
  CHECK(HTTPTransactionEgressSM::transit(
      egressState_, HTTPTransactionEgressSM::Event::sendEOM))
      << ", " << *this;
  if (expectedResponseLength_ && actualResponseLength_ &&
      (*expectedResponseLength_ != *actualResponseLength_)) {
    auto errorMsg =
        folly::to<std::string>("Content-Length/body mismatch: expected= ",
                               *expectedResponseLength_,
                               ", actual= ",
                               *actualResponseLength_);
    LOG(ERROR) << errorMsg << " " << *this;
  }

  if (deferredEgressBody_.chainLength() == 0 && chunkHeaders_.empty()) {
    // there is nothing left to send, egress the EOM directly.  For SPDY
    // this will jump the txn queue
    if (!isEnqueued()) {
      size_t nbytes = sendEOMNow();
      transport_.notifyPendingEgress();
      if (transportCallback_) {
        transportCallback_->bodyBytesGenerated(nbytes);
      }
    } else {
      // If the txn is enqueued, sendDeferredBody()
      // should take care of sending the EOM.
      // This can happen for some uses of the egress queue
      VLOG(4) << "Queued egress EOM with no body"
              << "[egressState=" << egressState_ << ", "
              << "ingressState=" << ingressState_ << ", "
              << "egressPaused=" << egressPaused_ << ", "
              << "ingressPaused=" << ingressPaused_ << ", "
              << "aborted=" << aborted_ << ", "
              << "enqueued=" << isEnqueued() << ", "
              << "chainLength=" << deferredEgressBody_.chainLength() << "]"
              << " on " << *this;
    }
  } else {
    VLOG(4) << "Queued egress EOM on " << *this;
    notifyTransportPendingEgress();
  }
}

void HTTPTransaction::sendAbort() {
  sendAbort(isUpstream() ? ErrorCode::CANCEL : ErrorCode::INTERNAL_ERROR);
}

void HTTPTransaction::sendAbort(ErrorCode statusCode) {
  DestructorGuard g(this);
  markIngressComplete();
  markEgressComplete();
  if (aborted_) {
    // This can happen in cases where the abort is sent before notifying the
    // handler, but its logic also wants to abort
    VLOG(4) << "skipping redundant abort";
    return;
  }
  VLOG(4) << "aborting transaction " << *this;
  aborted_ = true;
  size_t nbytes = transport_.sendAbort(this, statusCode);
  if (transportCallback_) {
    HTTPHeaderSize size;
    size.uncompressed = nbytes;
    transportCallback_->headerBytesGenerated(size);
  }
}

void HTTPTransaction::trimDeferredEgressBody(uint64_t bodyOffset) {
  CHECK(!useFlowControl_)
      << ": trimming egress deferred body with flow control enabled";

  if (deferredEgressBody_.chainLength() == 0) {
    // Nothing to trim.
    return;
  }

  // We only need to trim buffered bytes that are over those already committed.
  // So if the new offset is below what we already gave to the transport, just
  // return.
  if (bodyOffset <= egressBodyBytesCommittedToTransport_) {
    return;
  }

  auto bytesToTrim = bodyOffset - egressBodyBytesCommittedToTransport_;
  // Update committed offset to the new skip offset.
  egressBodyBytesCommittedToTransport_ = bodyOffset;
  auto trimmedBytes = deferredEgressBody_.trimStartAtMost(bytesToTrim);

  if (trimmedBytes > 0) {
    VLOG(3) << __func__ << ": trimmed " << trimmedBytes
            << " bytes from pending egress body";
    notifyTransportPendingEgress();
  }
}

folly::Expected<folly::Unit, ErrorCode> HTTPTransaction::peek(
    PeekCallback peekCallback) {
  return transport_.peek(peekCallback);
}

folly::Expected<folly::Unit, ErrorCode> HTTPTransaction::consume(
    size_t amount) {
  return transport_.consume(amount);
}

folly::Expected<folly::Optional<uint64_t>, ErrorCode>
HTTPTransaction::skipBodyTo(uint64_t nextBodyOffset) {
  if (!partiallyReliable_) {
    LOG(ERROR) << __func__
               << ": not permitted on non-partially reliable transaction.";
    return folly::makeUnexpected(ErrorCode::PROTOCOL_ERROR);
  }

  if (!egressHeadersDelivered_) {
    LOG(ERROR) << __func__
               << ": cannot send data expired before egress headers have been "
                  "delivered.";
    return folly::makeUnexpected(ErrorCode::PROTOCOL_ERROR);
  }

  // Trim buffered egress body to nextBodyOffset.
  trimDeferredEgressBody(nextBodyOffset);

  if (nextBodyOffset > actualResponseLength_.value()) {
    actualResponseLength_ = nextBodyOffset;
  }
  return transport_.skipBodyTo(this, nextBodyOffset);
}

folly::Expected<folly::Optional<uint64_t>, ErrorCode>
HTTPTransaction::rejectBodyTo(uint64_t nextBodyOffset) {
  if (!partiallyReliable_) {
    LOG(ERROR) << __func__
               << ": not permitted on non-partially reliable transaction.";
    return folly::makeUnexpected(ErrorCode::PROTOCOL_ERROR);
  }

  if (expectedIngressContentLength_ && expectedIngressContentLengthRemaining_) {
    if (nextBodyOffset > *expectedIngressContentLength_) {
      return folly::makeUnexpected(ErrorCode::PROTOCOL_ERROR);
    }

    auto currentBodyOffset = *expectedIngressContentLength_ -
                             *expectedIngressContentLengthRemaining_;

    if (nextBodyOffset <= currentBodyOffset) {
      return folly::makeUnexpected(ErrorCode::PROTOCOL_ERROR);
    }

    expectedIngressContentLengthRemaining_ =
        *expectedIngressContentLength_ - nextBodyOffset;
  }

  if (nextBodyOffset <= ingressBodyOffset_) {
    // Do not allow rejecting below already received body offset.
    LOG(ERROR) << ": cannot reject body below already received offset; "
                  "current received offset = "
               << ingressBodyOffset_
               << "; provided reject offset = " << nextBodyOffset;
    return folly::makeUnexpected(ErrorCode::PROTOCOL_ERROR);
  }
  ingressBodyOffset_ = nextBodyOffset;

  return transport_.rejectBodyTo(this, nextBodyOffset);
}

folly::Optional<HTTPTransaction::ConnectionToken>
HTTPTransaction::getConnectionToken() const noexcept {
  return transport_.getConnectionToken();
}

void HTTPTransaction::pauseIngress() {
  VLOG(4) << "pauseIngress request " << *this;
  DestructorGuard g(this);
  if (ingressPaused_) {
    VLOG(4) << "can't pause ingress; ingressPaused=" << ingressPaused_;
    return;
  }
  ingressPaused_ = true;
  cancelTimeout();
  transport_.pauseIngress(this);
}

void HTTPTransaction::resumeIngress() {
  VLOG(4) << "resumeIngress request " << *this;
  DestructorGuard g(this);
  if (!ingressPaused_ || isIngressComplete()) {
    VLOG(4) << "can't resume ingress, ingressPaused=" << ingressPaused_
            << ", ingressComplete=" << isIngressComplete()
            << ", inResume_=" << inResume_ << " " << *this;
    return;
  }
  ingressPaused_ = false;
  transport_.resumeIngress(this);
  if (inResume_) {
    VLOG(4) << "skipping recursive resume loop " << *this;
    return;
  }
  inResume_ = true;

  if (deferredIngress_ && (maxDeferredIngress_ <= deferredIngress_->size())) {
    maxDeferredIngress_ = deferredIngress_->size();
  }

  // Process any deferred ingress callbacks
  // Note: we recheck the ingressPaused_ state because a callback
  // invoked by the resumeIngress() call above could have re-paused
  // the transaction.
  while (!ingressPaused_ && deferredIngress_ && !deferredIngress_->empty()) {
    HTTPEvent& callback(deferredIngress_->front());
    VLOG(5) << "Processing deferred ingress callback of type "
            << callback.getEvent() << " " << *this;
    switch (callback.getEvent()) {
      case HTTPEvent::Type::MESSAGE_BEGIN:
        LOG(FATAL) << "unreachable";
        break;
      case HTTPEvent::Type::HEADERS_COMPLETE:
        processIngressHeadersComplete(callback.getHeaders());
        break;
      case HTTPEvent::Type::BODY: {
        unique_ptr<IOBuf> data = callback.getBody();
        auto len = data->computeChainDataLength();
        CHECK(recvWindow_.free(len));
        processIngressBody(std::move(data), len);
      } break;
      case HTTPEvent::Type::CHUNK_HEADER:
        processIngressChunkHeader(callback.getChunkLength());
        break;
      case HTTPEvent::Type::CHUNK_COMPLETE:
        processIngressChunkComplete();
        break;
      case HTTPEvent::Type::TRAILERS_COMPLETE:
        processIngressTrailers(callback.getTrailers());
        break;
      case HTTPEvent::Type::MESSAGE_COMPLETE:
        processIngressEOM();
        break;
      case HTTPEvent::Type::UPGRADE:
        processIngressUpgrade(callback.getUpgradeProtocol());
        break;
    }
    if (deferredIngress_) {
      deferredIngress_->pop();
    }
  }
  updateReadTimeout();
  inResume_ = false;
}

void HTTPTransaction::pauseEgress() {
  VLOG(4) << "asked to pause egress " << *this;
  DestructorGuard g(this);
  if (egressPaused_) {
    VLOG(4) << "egress already paused " << *this;
    return;
  }
  egressPaused_ = true;
  updateHandlerPauseState();
}

void HTTPTransaction::resumeEgress() {
  VLOG(4) << "asked to resume egress " << *this;
  DestructorGuard g(this);
  if (!egressPaused_) {
    VLOG(4) << "egress already not paused " << *this;
    return;
  }
  egressPaused_ = false;
  updateHandlerPauseState();
}

void HTTPTransaction::setEgressRateLimit(uint64_t bitsPerSecond) {
  egressLimitBytesPerMs_ = bitsPerSecond / 8000;
  if (bitsPerSecond > 0 && egressLimitBytesPerMs_ == 0) {
    VLOG(4) << "ratelim: Limit too low (" << bitsPerSecond << "), ignoring";
  }
  startRateLimit_ = getCurrentTime();
  numLimitedBytesEgressed_ = 0;
}

void HTTPTransaction::notifyTransportPendingEgress() {
  DestructorGuard guard(this);
  if (!egressRateLimited_ &&
      (deferredEgressBody_.chainLength() > 0 || isEgressEOMQueued()) &&
      (!useFlowControl_ || sendWindow_.getSize() > 0)) {
    // Egress isn't paused, we have something to send, and flow
    // control isn't blocking us.
    if (!isEnqueued()) {
      // Insert into the queue and let the session know we've got something
      egressQueue_.signalPendingEgress(queueHandle_);
      transport_.notifyPendingEgress();
      transport_.notifyEgressBodyBuffered(deferredEgressBody_.chainLength());
    }
  } else if (isEnqueued()) {
    // Nothing to send, or not allowed to send right now.
    int64_t deferredEgressBodyBytes =
        folly::to<int64_t>(deferredEgressBody_.chainLength());
    transport_.notifyEgressBodyBuffered(-deferredEgressBodyBytes);
    egressQueue_.clearPendingEgress(queueHandle_);
  }
  updateHandlerPauseState();
}

void HTTPTransaction::updateHandlerPauseState() {
  if (isEgressEOMSeen()) {
    VLOG(4) << "transaction already egress complete, not updating pause state "
            << *this;
    return;
  }
  int64_t availWindow =
      sendWindow_.getSize() - deferredEgressBody_.chainLength();
  // do not count transaction stalled if no more bytes to send,
  // i.e. when availWindow == 0
  if (useFlowControl_ && availWindow < 0 && !flowControlPaused_) {
    VLOG(4) << "transaction stalled by flow control txn=" << *this;
    if (stats_) {
      stats_->recordTransactionStalled();
    }
  }
  flowControlPaused_ = useFlowControl_ && availWindow <= 0;
  bool bufferFull = deferredEgressBody_.chainLength() > kMaxBufferPerTxn;
  bool handlerShouldBePaused =
      egressPaused_ || flowControlPaused_ || egressRateLimited_ || bufferFull;

  if (!egressPaused_ && bufferFull) {
    VLOG(4) << "Not resuming handler, buffer full, txn=" << *this;
  }

  if (handler_ && handlerShouldBePaused != handlerEgressPaused_) {
    if (handlerShouldBePaused) {
      handlerEgressPaused_ = true;
      VLOG(4) << "egress paused txn=" << *this;
      handler_->onEgressPaused();
    } else {
      handlerEgressPaused_ = false;
      VLOG(4) << "egress resumed txn=" << *this;
      handler_->onEgressResumed();
    }
  }
}

void HTTPTransaction::updateIngressCompressionInfo(
    const CompressionInfo& tableInfo) {
  tableInfo_.ingress = tableInfo.ingress;
}

void HTTPTransaction::updateEgressCompressionInfo(
    const CompressionInfo& tableInfo) {
  tableInfo_.egress = tableInfo.egress;
}

const CompressionInfo& HTTPTransaction::getCompressionInfo() const {
  return tableInfo_;
}

bool HTTPTransaction::mustQueueIngress() const {
  return ingressPaused_ || (deferredIngress_ && !deferredIngress_->empty());
}

void HTTPTransaction::checkCreateDeferredIngress() {
  if (!deferredIngress_) {
    deferredIngress_ = std::make_unique<std::queue<HTTPEvent>>();
  }
}

bool HTTPTransaction::onPushedTransaction(HTTPTransaction* pushTxn) {
  DestructorGuard g(this);
  CHECK_EQ(*pushTxn->assocStreamId_, id_);
  if (!handler_) {
    VLOG(4) << "Cannot add a pushed txn to an unhandled txn";
    return false;
  }
  refreshTimeout();
  handler_->onPushedTransaction(pushTxn);
  if (!pushTxn->getHandler()) {
    VLOG(4) << "Failed to create a handler for push transaction";
    return false;
  }
  pushedTransactions_.insert(pushTxn->getID());
  return true;
}

bool HTTPTransaction::onExTransaction(HTTPTransaction* exTxn) {
  DestructorGuard g(this);
  CHECK_EQ(*(exTxn->getControlStream()), id_);
  if (!handler_) {
    LOG(ERROR) << "Cannot add a exTxn to an unhandled txn";
    return false;
  }
  handler_->onExTransaction(exTxn);
  if (!exTxn->getHandler()) {
    LOG(ERROR) << "Failed to create a handler for ExTransaction";
    return false;
  }
  exTransactions_.insert(exTxn->getID());
  return true;
}

void HTTPTransaction::setIdleTimeout(
    std::chrono::milliseconds transactionTimeout) {
  transactionTimeout_ = transactionTimeout;
  VLOG(4) << "HTTPTransaction: transaction timeout is set to  "
          << std::chrono::duration_cast<std::chrono::milliseconds>(
                 transactionTimeout)
                 .count();
  refreshTimeout();
}

void HTTPTransaction::describe(std::ostream& os) const {
  transport_.describe(os);
  os << ", streamID=" << id_;
}

/*
 * TODO: when HTTPSession sends a SETTINGS frame indicating a
 * different initial window, it should call this function on all its
 * transactions.
 */
void HTTPTransaction::setReceiveWindow(uint32_t capacity) {
  // Depending on whether delta is positive or negative it will cause the
  // window to either increase or decrease.
  if (!useFlowControl_) {
    return;
  }
  int32_t delta = capacity - recvWindow_.getCapacity();
  if (delta < 0) {
    // For now, we're disallowing shrinking the window, since it can lead
    // to FLOW_CONTROL_ERRORs if there is data in flight.
    VLOG(4) << "Refusing to shrink the recv window";
    return;
  }
  if (!recvWindow_.setCapacity(capacity)) {
    return;
  }
  recvToAck_ += delta;
  flushWindowUpdate();
}

void HTTPTransaction::flushWindowUpdate() {
  if (recvToAck_ > 0 && useFlowControl_ && !isIngressEOMSeen() &&
      (direction_ == TransportDirection::DOWNSTREAM ||
       egressState_ != HTTPTransactionEgressSM::State::Start ||
       ingressState_ != HTTPTransactionIngressSM::State::Start)) {
    // Down egress upstream window updates until after headers
    VLOG(4) << "recv_window is " << recvWindow_.getSize() << " / "
            << recvWindow_.getCapacity() << " after acking " << recvToAck_
            << " " << *this;
    transport_.sendWindowUpdate(this, recvToAck_);
    recvToAck_ = 0;
  }
}

int32_t HTTPTransaction::getRecvToAck() const {
  return recvToAck_;
}

std::ostream& operator<<(std::ostream& os, const HTTPTransaction& txn) {
  txn.describe(os);
  return os;
}

void HTTPTransaction::updateAndSendPriority(int8_t newPriority) {
  newPriority = HTTPMessage::normalizePriority(newPriority);
  CHECK_GE(newPriority, 0);
  priority_.streamDependency =
      transport_.getCodec().mapPriorityToDependency(newPriority);
  queueHandle_ = egressQueue_.updatePriority(queueHandle_, priority_);
  transport_.sendPriority(this, priority_);
}

void HTTPTransaction::updateAndSendPriority(
    const http2::PriorityUpdate& newPriority) {
  onPriorityUpdate(newPriority);
  transport_.sendPriority(this, priority_);
}

void HTTPTransaction::onPriorityUpdate(const http2::PriorityUpdate& priority) {
  priority_ = priority;

  queueHandle_ =
      egressQueue_.updatePriority(queueHandle_, priority_, &currentDepth_);
  if (priority_.streamDependency != egressQueue_.getRootId() &&
      currentDepth_ == 1) {
    priorityFallback_ = true;
  }
}

class HTTPTransaction::PrioritySample {
  struct WeightedAccumulator {
    void accumulate(uint64_t weighted, uint64_t total) {
      weighted_ += weighted;
      total_ += total;
    }

    void accumulateWeighted(uint64_t weighted) {
      weighted_ += weighted;
    }

    void accumulateTotal(uint64_t total) {
      total_ += total;
    }

    double getWeightedAverage() const {
      return total_ ? (double)weighted_ / (double)total_ : 0;
    }

   private:
    uint64_t weighted_{0};
    uint64_t total_{0};
  };

  struct WeightedValue {
    uint64_t value_{0};

    void accumulateByTransactionBytes(uint64_t bytes) {
      byTransactionBytesSent_.accumulate(value_ * bytes, bytes);
    }

    void accumulateBySessionBytes(uint64_t bytes) {
      bySessionBytesScheduled_.accumulate(value_ * bytes, bytes);
    }

    void getSummary(
        HTTPTransaction::PrioritySampleSummary::WeightedAverage& wa) const {
      wa.byTransactionBytes_ = byTransactionBytesSent_.getWeightedAverage();
      wa.bySessionBytes_ = bySessionBytesScheduled_.getWeightedAverage();
    }

   private:
    WeightedAccumulator byTransactionBytesSent_;
    WeightedAccumulator bySessionBytesScheduled_;
  };

 public:
  explicit PrioritySample(HTTPTransaction* tnx)
      : tnx_(tnx), transactionBytesScheduled_(false) {
  }

  void updateContentionsCount(uint64_t contentions, uint64_t depth) {
    transactionBytesScheduled_ = false;
    ratio_ = 0.0;
    contentions_.value_ = contentions;
    depth_.value_ = depth;
  }

  void updateTransactionBytesSent(uint64_t bytes) {
    transactionBytesScheduled_ = true;
    measured_weight_.accumulateWeighted(bytes);
    if (contentions_.value_) {
      contentions_.accumulateByTransactionBytes(bytes);
    } else {
      VLOG(5) << "transfer " << bytes
              << " transaction body bytes while contentions count = 0 "
              << *tnx_;
    }
    depth_.accumulateByTransactionBytes(bytes);
  }

  void updateSessionBytesSheduled(uint64_t bytes) {
    measured_weight_.accumulateTotal(bytes);
    expected_weight_.accumulate((ratio_ * bytes) + 0.5, bytes);
    if (contentions_.value_) {
      contentions_.accumulateBySessionBytes(bytes);
    } else {
      VLOG(5) << "transfer " << bytes
              << " session body bytes while contentions count = 0 " << *tnx_;
    }
    depth_.accumulateBySessionBytes(bytes);
  }

  void updateRatio(double ratio) {
    ratio_ = ratio;
  }

  bool isTransactionBytesScheduled() const {
    return transactionBytesScheduled_;
  }

  void getSummary(HTTPTransaction::PrioritySampleSummary& summary) const {
    contentions_.getSummary(summary.contentions_);
    depth_.getSummary(summary.depth_);
    summary.expected_weight_ = expected_weight_.getWeightedAverage();
    summary.measured_weight_ = measured_weight_.getWeightedAverage();
  }

 private:
  // TODO: remove tnx_ when not needed
  HTTPTransaction* tnx_; // needed for error reporting, will be removed
  WeightedValue contentions_;
  WeightedValue depth_;
  WeightedAccumulator expected_weight_;
  WeightedAccumulator measured_weight_;
  double ratio_;
  bool transactionBytesScheduled_ : 1;
};

void HTTPTransaction::setPrioritySampled(bool sampled) {
  if (sampled) {
    prioritySample_ = std::make_unique<PrioritySample>(this);
  } else {
    prioritySample_.reset();
  }
}

void HTTPTransaction::updateContentionsCount(uint64_t contentions) {
  CHECK(prioritySample_);
  prioritySample_->updateContentionsCount(contentions,
                                          queueHandle_->calculateDepth(false));
}

void HTTPTransaction::updateRelativeWeight(double ratio) {
  CHECK(prioritySample_);
  prioritySample_->updateRatio(ratio);
}

void HTTPTransaction::updateSessionBytesSheduled(uint64_t bytes) {
  CHECK(prioritySample_);
  // Do not accumulate session bytes utill header is sent.
  // Otherwise, the session bytes could be accumulated for a transaction
  // that is not allowed to egress yet.
  // Do not accumulate session bytes if transaction is paused.
  // On the other hand, if the transaction is part of the egress,
  // always accumulate the session bytes.
  if ((bytes && firstHeaderByteSent_ && !egressPaused_ && !egressRateLimited_ &&
       !flowControlPaused_) ||
      prioritySample_->isTransactionBytesScheduled()) {
    prioritySample_->updateSessionBytesSheduled(bytes);
  }
}

void HTTPTransaction::updateTransactionBytesSent(uint64_t bytes) {
  CHECK(prioritySample_);
  if (bytes) {
    prioritySample_->updateTransactionBytesSent(bytes);
  }
}

void HTTPTransaction::checkIfEgressRateLimitedByUpstream() {
  if (transportCallback_ && !isEgressEOMQueued() &&
      deferredEgressBody_.chainLength() == 0) {
    transportCallback_->egressBufferEmpty();
  }
}

bool HTTPTransaction::getPrioritySampleSummary(
    HTTPTransaction::PrioritySampleSummary& summary) const {
  if (prioritySample_) {
    prioritySample_->getSummary(summary);
    return true;
  }
  return false;
}

} // namespace proxygen
