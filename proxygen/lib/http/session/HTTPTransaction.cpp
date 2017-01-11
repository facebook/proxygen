/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPTransaction.h>

#include <algorithm>
#include <folly/Conv.h>
#include <folly/io/async/EventBaseManager.h>
#include <glog/logging.h>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/RFC2616.h>
#include <proxygen/lib/http/codec/SPDYConstants.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>

using folly::IOBuf;
using std::unique_ptr;

namespace proxygen {

namespace {
  const int64_t kApproximateMTU = 1400;
  const std::chrono::seconds kRateLimitMaxDelay(10);
}

HTTPTransaction::HTTPTransaction(TransportDirection direction,
                                 HTTPCodec::StreamID id,
                                 uint32_t seqNo,
                                 Transport& transport,
                                 HTTP2PriorityQueue& egressQueue,
                                 const WheelTimerInstance& timeout,
                                 HTTPSessionStats* stats,
                                 bool useFlowControl,
                                 uint32_t receiveInitialWindowSize,
                                 uint32_t sendInitialWindowSize,
                                 http2::PriorityUpdate priority,
                                 HTTPCodec::StreamID assocId):
    deferredEgressBody_(folly::IOBufQueue::cacheChainLength()),
    direction_(direction),
    id_(id),
    seqNo_(seqNo),
    transport_(transport),
    timeout_(timeout),
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
    inActiveSet_(true),
    ingressErrorSeen_(false),
    priorityFallback_(false),
    headRequest_(false) {

  if (assocStreamId_) {
    if (isUpstream()) {
      egressState_ = HTTPTransactionEgressSM::State::SendingDone;
    } else {
      ingressState_ = HTTPTransactionIngressSM::State::ReceivingDone;
    }
  }

  refreshTimeout();
  if (stats_) {
    stats_->recordTransactionOpened();
  }

  queueHandle_ = egressQueue_.addTransaction(id_, priority, this, false,
                                             &insertDepth_);
  if(priority.streamDependency != 0 && insertDepth_ == 1) {
    priorityFallback_ = true;
  }

  currentDepth_ = insertDepth_;
}

void HTTPTransaction::onDelayedDestroy(bool delayed) {
  if (!isEgressComplete() || !isIngressComplete() || isEnqueued()
      || deleting_) {
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
  if ((msg->isRequest() && msg->getMethod() != HTTPMethod::CONNECT) ||
       (msg->isResponse() &&
        !headRequest_ &&
        !RFC2616::responseBodyMustBeEmpty(msg->getStatusCode()))) {
    // CONNECT payload has no defined semantics
    const auto& clHeader =
      msg->getHeaders().getSingleOrEmpty(HTTP_HEADER_CONTENT_LENGTH);
    if (!clHeader.empty()) {
      try {
        expectedContentLengthRemaining_ = folly::to<uint64_t>(clHeader);
      } catch (const folly::ConversionError& ex) {
        // ignore this, at least for now
      }
    }
  }
  if (transportCallback_) {
    transportCallback_->headerBytesReceived(msg->getIngressHeaderSize());
  }
  if (mustQueueIngress()) {
    checkCreateDeferredIngress();
    deferredIngress_->emplace(id_, HTTPEvent::Type::HEADERS_COMPLETE,
                             std::move(msg));
    VLOG(4) << *this << " Queued ingress event of type " <<
      HTTPEvent::Type::HEADERS_COMPLETE;
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

void HTTPTransaction::onIngressBody(unique_ptr<IOBuf> chain,
                                    uint16_t padding) {
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
  if (expectedContentLengthRemaining_.hasValue()) {
    if (expectedContentLengthRemaining_.value() >= len) {
      expectedContentLengthRemaining_ =
        expectedContentLengthRemaining_.value() - len;
    } else {
      auto errorMsg = folly::to<std::string>(
          "Content-Length/body mismatch: received=",
          len,
          " expecting no more than ",
          expectedContentLengthRemaining_.value());
      LOG(ERROR) << *this << " " << errorMsg;
      if (handler_) {
        HTTPException ex(HTTPException::Direction::INGRESS, errorMsg);
        ex.setProxygenError(kErrorParseBody);
        onError(ex);
      }
      return;
    }
  }
  if (transportCallback_) {
    transportCallback_->bodyBytesReceived(len);
  }
  if (mustQueueIngress()) {
    // register the bytes in the receive window
    if (!recvWindow_.reserve(len + padding, useFlowControl_)) {
      LOG(ERROR) << *this << " recvWindow_.reserve failed with len=" << len <<
        " padding=" << padding << " capacity=" << recvWindow_.getCapacity() <<
        " outstanding=" << recvWindow_.getOutstanding();
      sendAbort(ErrorCode::FLOW_CONTROL_ERROR);
    } else {
      CHECK(recvWindow_.free(padding));
      recvToAck_ += padding;
      checkCreateDeferredIngress();
      deferredIngress_->emplace(id_, HTTPEvent::Type::BODY,
                               std::move(chain));
      VLOG(4) << *this << " Queued ingress event of type " <<
        HTTPEvent::Type::BODY << " size=" << len;
    }
  } else {
    processIngressBody(std::move(chain), len);
  }
}

void HTTPTransaction::processIngressBody(unique_ptr<IOBuf> chain, size_t len) {
  DestructorGuard g(this);
  if (aborted_) {
    return;
  }
  refreshTimeout();
  transport_.notifyIngressBodyProcessed(len);
  if (handler_) {
    if (!isIngressComplete()) {
      handler_->onBody(std::move(chain));
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
}

void HTTPTransaction::onIngressChunkHeader(size_t length) {
  if (!validateIngressStateTransition(
        HTTPTransactionIngressSM::Event::onChunkHeader)) {
    return;
  }
  if (mustQueueIngress()) {
    checkCreateDeferredIngress();
    deferredIngress_->emplace(id_, HTTPEvent::Type::CHUNK_HEADER, length);
    VLOG(4) << *this << " Queued ingress event of type " <<
      HTTPEvent::Type::CHUNK_HEADER << " size=" << length;
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
    VLOG(4) << *this << " Queued ingress event of type " <<
      HTTPEvent::Type::CHUNK_COMPLETE;
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
    deferredIngress_->emplace(id_, HTTPEvent::Type::TRAILERS_COMPLETE,
        std::move(trailers));
    VLOG(4) << *this << " Queued ingress event of type " <<
      HTTPEvent::Type::TRAILERS_COMPLETE;
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
    VLOG(4) << *this << " Queued ingress event of type " <<
      HTTPEvent::Type::UPGRADE;
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
  if (expectedContentLengthRemaining_.hasValue() &&
      expectedContentLengthRemaining_.value() > 0) {
    auto errorMsg = folly::to<std::string>(
        "Content-Length/body mismatch: expecting another ",
        expectedContentLengthRemaining_.value());
    LOG(ERROR) << *this << " " << errorMsg;
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
  if (!validateIngressStateTransition(
        HTTPTransactionIngressSM::Event::onEOM)) {
    return;
  }
  // We need to update the read timeout here.  We're not likely to be
  // expecting any more ingress, and the timer should be cancelled
  // immediately.  If we are expecting more, this will reset the timer.
  updateReadTimeout();
  if (mustQueueIngress()) {
    checkCreateDeferredIngress();
    deferredIngress_->emplace(id_, HTTPEvent::Type::MESSAGE_COMPLETE);
    VLOG(4) << *this << " Queued ingress event of type " <<
      HTTPEvent::Type::MESSAGE_COMPLETE;
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
  return (useFlowControl_ && sendWindow_.getSize() <= 0);
}

bool HTTPTransaction::isExpectingIngress() const {
  return (!ingressPaused_ &&
          (!isIngressEOMSeen() || isExpectingWindowUpdate()));
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
    ss << "Invalid ingress state transition, state=" << ingressState_ <<
      ", event=" << event << ", streamID=" << id_;
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

  if (direction == HTTPException::Direction::INGRESS &&
      isIngressEOMSeen() && isExpectingWindowUpdate()) {
    // we got an ingress error, we've seen the entire message, but we're
    // expecting more (window updates).  These aren't coming, convert to
    // INGRESS_AND_EGRESS
    VLOG(4) << *this << " Converting ingress error to ingress+egress due to"
      " flow control, and aborting";
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
      if (wasEgressComplete) {
        notify = false;
      }
      break;
    case HTTPException::Direction::INGRESS:
      if (isIngressEOMSeen()) {
        // Not an error, for now
        ingressErrorSeen_ = true;
        return;
      }
      markIngressComplete();
      if (wasIngressComplete) {
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
      HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
          folly::to<std::string>("ingress timeout, streamID=", id_));
      ex.setProxygenError(kErrorWriteTimeout);
      // This is a protocol error
      ex.setCodecStatusCode(ErrorCode::PROTOCOL_ERROR);
      onError(ex);
    } else {
      HTTPException ex(HTTPException::Direction::INGRESS,
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
  VLOG(4) << *this << " Remote side ack'd " << amount << " bytes";
  updateReadTimeout();
  if (sendWindow_.free(amount)) {
    notifyTransportPendingEgress();
  } else {
    LOG(ERROR) << *this << "sendWindow_.free failed with amount=" << amount <<
      " capacity=" << sendWindow_.getCapacity() <<
      " outstanding=" << sendWindow_.getOutstanding();
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
    LOG(ERROR) << *this << "sendWindow_.setCapacity failed with newWindowSize="
               << newWindowSize << " capacity=" << sendWindow_.getCapacity()
               << " outstanding=" << sendWindow_.getOutstanding();
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

void HTTPTransaction::onEgressLastByteAck(std::chrono::milliseconds latency) {
  DestructorGuard g(this);
  if (transportCallback_) {
    transportCallback_->lastByteAcked(latency);
  }
}

void HTTPTransaction::sendHeadersWithOptionalEOM(
    const HTTPMessage& headers,
    bool eom) {
  CHECK(HTTPTransactionEgressSM::transit(
          egressState_, HTTPTransactionEgressSM::Event::sendHeaders));
  DCHECK(!isEgressComplete());
  if (isDownstream() && !isPushed()) {
    lastResponseStatus_ = headers.getStatusCode();
  }
  if (headers.isRequest()) {
    headRequest_ = (headers.getMethod() == HTTPMethod::HEAD);
  }
  HTTPHeaderSize size;
  transport_.sendHeaders(this, headers, &size, eom);
  if (transportCallback_) {
    transportCallback_->headerBytesGenerated(size);
  }
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
  sendHeadersWithOptionalEOM(header, false);
}

void HTTPTransaction::sendBody(std::unique_ptr<folly::IOBuf> body) {
  DestructorGuard guard(this);
  CHECK(HTTPTransactionEgressSM::transit(
      egressState_, HTTPTransactionEgressSM::Event::sendBody));
  if (body && isEnqueued()) {
    size_t bodyLen = body->computeChainDataLength();
    transport_.notifyEgressBodyBuffered(bodyLen);
  }
  deferredEgressBody_.append(std::move(body));
  notifyTransportPendingEgress();
}

bool HTTPTransaction::onWriteReady(const uint32_t maxEgress, double ratio) {
  DestructorGuard g(this);
  DCHECK(isEnqueued());
  cumulativeRatio_ += ratio;
  egressCalls_++;
  sendDeferredBody(maxEgress);
  return isEnqueued();
}

// Send up to maxEgress body bytes, including pendingEOM if appropriate
size_t HTTPTransaction::sendDeferredBody(const uint32_t maxEgress) {
  const int32_t windowAvailable = sendWindow_.getSize();
  const uint32_t sendWindow = useFlowControl_ ? std::min<uint32_t>(
    maxEgress, windowAvailable > 0 ? windowAvailable : 0) : maxEgress;

  // We shouldn't be called if we have no pending body/EOM, egress is paused, or
  // the send window is closed
  CHECK((deferredEgressBody_.chainLength() > 0 ||
         isEgressEOMQueued()) &&
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

  int64_t limitedDurationMs = (int64_t) millisecondsBetween(
    getCurrentTime(),
    startRateLimit_
  ).count();

  // Algebra!  Try to figure out the next time send where we'll
  // be allowed to send at least 1 full packet's worth.  The
  // formula we're using is:
  //   (bytesSoFar + packetSize) / (timeSoFar + delay) == targetRateLimit
  std::chrono::milliseconds requiredDelay(
    (
     ((int64_t)numLimitedBytesEgressed_ + kApproximateMTU) -
     ((int64_t)egressLimitBytesPerMs_ * limitedDurationMs)
    ) / (int64_t)egressLimitBytesPerMs_
  );

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

  timeout_.scheduleTimeout(&rateLimitCallback_, requiredDelay);

  notifyTransportPendingEgress();
  return true;
}

void HTTPTransaction::rateLimitTimeoutExpired() {
  egressRateLimited_ = false;
  notifyTransportPendingEgress();
}

size_t HTTPTransaction::sendEOMNow() {
  size_t nbytes = 0;
  VLOG(4) << "egress EOM on " << *this;
  if (trailers_) {
    VLOG(4) << "egress trailers on " << *this;
    nbytes += transport_.sendTrailers(this, *trailers_.get());
    trailers_.reset();
  }
  // TODO: with ByteEvent refactor, we will have to delay changing this
  // state until later
  CHECK(HTTPTransactionEgressSM::transit(
          egressState_, HTTPTransactionEgressSM::Event::eomFlushed));
  nbytes += transport_.sendEOM(this);
  return nbytes;
}

size_t HTTPTransaction::sendBodyNow(std::unique_ptr<folly::IOBuf> body,
                                    size_t bodyLen, bool sendEom) {
  static const std::string noneStr = "None";
  DCHECK(body);
  DCHECK_GT(bodyLen, 0);
  size_t nbytes = 0;
  if (useFlowControl_) {
    CHECK(sendWindow_.reserve(bodyLen));
  }
  VLOG(4) << *this << " Sending " << bodyLen << " bytes of body. eom="
          << ((sendEom) ? "yes" : "no") << " send_window is "
          << ( useFlowControl_ ?
               folly::to<std::string>(sendWindow_.getSize(), " / ",
                                      sendWindow_.getCapacity()) : noneStr);
  if (sendEom) {
    CHECK(HTTPTransactionEgressSM::transit(
            egressState_, HTTPTransactionEgressSM::Event::eomFlushed));
  } else if (ingressErrorSeen_ && isExpectingWindowUpdate()) {
    // I don't know how we got here but we're in trouble.  We need a window
    // update to continue but we've already seen an ingress error.
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                     folly::to<std::string>("window blocked with ingress error,"
                                            " streamID=", id_));
    ex.setProxygenError(kErrorEOF);
    ex.setCodecStatusCode(ErrorCode::FLOW_CONTROL_ERROR);
    onError(ex);
    return 0;
  }
  updateReadTimeout();
  nbytes = transport_.sendBody(this, std::move(body), sendEom);
  if (egressLimitBytesPerMs_ > 0) {
    numLimitedBytesEgressed_ += nbytes;
  }
  return nbytes;
}

void
HTTPTransaction::sendEOM() {
  DestructorGuard g(this);
  CHECK(HTTPTransactionEgressSM::transit(
      egressState_, HTTPTransactionEgressSM::Event::sendEOM))
    << ", " << *this;
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
      // Nevertheless we never expect this condition to occur,
      // so, log.
      LOG(ERROR) << "Queued egress EOM with no body on "
        << *this
        << "[egressState=" << egressState_ << ", "
        << "ingressState=" << ingressState_ << ", "
        << "egressPaused=" << egressPaused_ << ", "
        << "ingressPaused=" << ingressPaused_ << ", "
        << "aborted=" << aborted_ << ", "
        << "enqueued=" << isEnqueued() << ", "
        << "chainLength=" << deferredEgressBody_.chainLength() << "]";
    }
  } else {
    VLOG(4) << "Queued egress EOM on " << *this;
    notifyTransportPendingEgress();
  }
}

void HTTPTransaction::sendAbort() {
  sendAbort(isUpstream() ? ErrorCode::CANCEL
                         : ErrorCode::INTERNAL_ERROR);
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

void HTTPTransaction::pauseIngress() {
  VLOG(4) << *this << " pauseIngress request";
  DestructorGuard g(this);
  if (ingressPaused_) {
    VLOG(4) << *this << " can't pause ingress; ingressPaused=" <<
        ingressPaused_;
    return;
  }
  ingressPaused_ = true;
  cancelTimeout();
  transport_.pauseIngress(this);
}

void HTTPTransaction::resumeIngress() {
  VLOG(4) << *this << " resumeIngress request";
  DestructorGuard g(this);
  if (!ingressPaused_ || isIngressComplete()) {
    VLOG(4) << *this << " can't resume ingress; ingressPaused="
            << ingressPaused_ << ", ingressComplete="
            << isIngressComplete() << " inResume_=" << inResume_;
    return;
  }
  ingressPaused_ = false;
  transport_.resumeIngress(this);
  if (inResume_) {
    VLOG(4) << *this << " skipping recursive resume loop";
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
    VLOG(5) << *this << " Processing deferred ingress callback of type " <<
      callback.getEvent();
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
  VLOG(4) << *this << " asked to pause egress";
  DestructorGuard g(this);
  if (egressPaused_) {
    VLOG(4) << *this << " egress already paused";
    return;
  }
  egressPaused_ = true;
  updateHandlerPauseState();
}

void HTTPTransaction::resumeEgress() {
  VLOG(4) << *this << " asked to resume egress";
  DestructorGuard g(this);
  if (!egressPaused_) {
    VLOG(4) << *this << " egress already not paused";
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
      (deferredEgressBody_.chainLength() > 0 ||
       isEgressEOMQueued()) &&
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
  int64_t availWindow =
    sendWindow_.getSize() - deferredEgressBody_.chainLength();
  // do not count transaction stalled if no more bytes to send,
  // i.e. when availWindow == 0
  if (useFlowControl_ && availWindow < 0 && !flowControlPaused_) {
    VLOG(4) << *this << " transaction stalled by flow control";
    if (stats_) {
      stats_->recordTransactionStalled();
    }
  }
  flowControlPaused_ = useFlowControl_ && availWindow <= 0;
  bool handlerShouldBePaused = egressPaused_ || flowControlPaused_ ||
    egressRateLimited_;

  if (handler_ && handlerShouldBePaused != handlerEgressPaused_) {
    if (handlerShouldBePaused) {
      handlerEgressPaused_ = true;
      handler_->onEgressPaused();
    } else {
      handlerEgressPaused_ = false;
      handler_->onEgressResumed();
    }
  }
}

bool HTTPTransaction::mustQueueIngress() const {
  return ingressPaused_ || (deferredIngress_ && !deferredIngress_->empty());
}

void HTTPTransaction::checkCreateDeferredIngress() {
  if (!deferredIngress_) {
    deferredIngress_ = folly::make_unique<std::queue<HTTPEvent>>();
  }
}

bool HTTPTransaction::onPushedTransaction(HTTPTransaction* pushTxn) {
  DestructorGuard g(this);
  CHECK_EQ(pushTxn->assocStreamId_, id_);
  if (!handler_) {
    VLOG(1) << "Cannot add a pushed txn to an unhandled txn";
    return false;
  }
  handler_->onPushedTransaction(pushTxn);
  if (!pushTxn->getHandler()) {
    VLOG(1) << "Failed to create a handler for push transaction";
    return false;
  }
  pushedTransactions_.insert(pushTxn->getID());
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
  os << " streamID=" << id_;
}

/*
 * TODO: when HTTPSession sends a SETTINGS frame indicating a
 * different initial window, it should call this function on all its
 * transactions.
 */
void HTTPTransaction::setReceiveWindow(uint32_t capacity) {
  // Depending on whether delta is positive or negative it will cause the
  // window to either increase or decrease.
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

  if (recvToAck_ > 0 &&
      (direction_ == TransportDirection::DOWNSTREAM ||
       egressState_ != HTTPTransactionEgressSM::State::Start)) {
    // Down egress upstream window updates until after headers
    VLOG(4) << *this << " recv_window is " << recvWindow_.getSize()
            << " / " << recvWindow_.getCapacity() << " after acking "
            << recvToAck_;
    transport_.sendWindowUpdate(this, recvToAck_);
    recvToAck_ = 0;
  }
}

int32_t HTTPTransaction::getRecvToAck() const {
  return recvToAck_;
}

std::ostream&
operator<<(std::ostream& os, const HTTPTransaction& txn) {
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

  queueHandle_ = egressQueue_.updatePriority(
      queueHandle_,
      priority_,
      &currentDepth_);
  if(priority_.streamDependency != 0 && currentDepth_ == 1) {
    priorityFallback_ = true;
  }
}

} // proxygen
