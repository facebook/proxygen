/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "proxygen/lib/http/session/HTTPTransaction.h"

#include "proxygen/lib/http/HTTPHeaderSize.h"
#include "proxygen/lib/http/codec/SPDYConstants.h"
#include "proxygen/lib/http/session/HTTPSessionStats.h"

#include <algorithm>
#include <glog/logging.h>

using apache::thrift::async::TAsyncTimeoutSet;
using folly::IOBuf;
using std::unique_ptr;

namespace proxygen {

uint64_t HTTPTransaction::egressBodySizeLimit_ = 4096;
uint64_t HTTPTransaction::egressBufferLimit_ = 8192;

HTTPTransaction::HTTPTransaction(TransportDirection direction,
                                 HTTPCodec::StreamID id,
                                 uint32_t seqNo,
                                 Transport& transport,
                                 PriorityQueue& egressQueue,
                                 TAsyncTimeoutSet* transactionTimeouts,
                                 HTTPSessionStats* stats,
                                 bool useFlowControl,
                                 uint32_t receiveInitialWindowSize,
                                 uint32_t sendInitialWindowSize,
                                 int8_t priority,
                                 HTTPCodec::StreamID assocId):
    deferredEgressBody_(folly::IOBufQueue::cacheChainLength()),
    direction_(direction),
    id_(id),
    seqNo_(seqNo),
    transport_(transport),
    transactionTimeouts_(transactionTimeouts),
    stats_(stats),
    recvWindow_(receiveInitialWindowSize),
    sendWindow_(sendInitialWindowSize),
    egressQueue_(egressQueue),
    assocStreamId_(assocId),
    priority_(priority << spdy::SPDY_PRIO_SHIFT_FACTOR),
    ingressPaused_(false),
    egressPaused_(false),
    handlerEgressPaused_(false),
    useFlowControl_(useFlowControl),
    aborted_(false),
    deleting_(false),
    enqueued_(false),
    firstByteSent_(false),
    firstHeaderByteSent_(false),
    inResume_(false),
    inActiveSet_(true) {

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
}

HTTPTransaction::~HTTPTransaction() {
  if (stats_) {
    stats_->recordTransactionClosed();
  }
  if (isEnqueued()) {
    dequeue();
  }
}

void HTTPTransaction::onIngressHeadersComplete(
  std::unique_ptr<HTTPMessage> msg) {
  msg->setSeqNo(seqNo_);
  if (transportCallback_) {
    transportCallback_->headerBytesReceived(msg->getIngressHeaderSize());
  }
  if (isUpstream() && !isPushed()) {
    lastResponseStatus_ = msg->getStatusCode();
  }
  if (!validateIngressStateTransition(
        HTTPTransactionIngressSM::Event::onHeaders)) {
    return;
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
  CallbackGuard guard(*this);
  if (aborted_) {
    return;
  }
  refreshTimeout();
  if (handler_ && !isIngressComplete()) {
    handler_->onHeadersComplete(std::move(msg));
  }
}

void HTTPTransaction::onIngressBody(unique_ptr<IOBuf> chain) {
  if (isIngressEOMSeen()) {
    sendAbort(ErrorCode::STREAM_CLOSED);
    return;
  }
  auto len = chain->computeChainDataLength();
  if (len == 0) {
    return;
  }
  if (transportCallback_) {
    transportCallback_->bodyBytesReceived(len);
  }
  if (!validateIngressStateTransition(
        HTTPTransactionIngressSM::Event::onBody)) {
    return;
  }
  if (mustQueueIngress()) {
    // register the bytes in the receive window
    if (!recvWindow_.reserve(len, useFlowControl_)) {
      sendAbort(ErrorCode::FLOW_CONTROL_ERROR);
    } else {
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
  CallbackGuard guard(*this);
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
        if (recvToAck_ >= 0 &&
            uint32_t(recvToAck_) >= (recvWindow_.getCapacity() / divisor)) {
          VLOG(4) << *this << " recv_window is " << recvWindow_.getSize()
                  << " / " << recvWindow_.getCapacity() << " after acking "
                  << recvToAck_;
          transport_.sendWindowUpdate(this, recvToAck_);
          recvToAck_ = 0;
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
  CallbackGuard guard(*this);
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
  CallbackGuard guard(*this);
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
  CallbackGuard guard(*this);
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
  CallbackGuard guard(*this);
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
  CallbackGuard guard(*this);
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

bool HTTPTransaction::isExpectingIngress() const {
  return (!ingressPaused_ &&
          (!isIngressEOMSeen() ||
           (useFlowControl_ && sendWindow_.getSize() <= 0)));
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
  deferredEgressBody_.move();
  if (isEnqueued()) {
    dequeue();
  }
  egressState_ = HTTPTransactionEgressSM::State::SendingDone;
}

bool HTTPTransaction::validateIngressStateTransition(
    HTTPTransactionIngressSM::Event event) {
  CallbackGuard guard(*this);

  if (!HTTPTransactionIngressSM::transit(ingressState_, event)) {
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS);
    ex.setProxygenError(kErrorIngressStateTransition);
    ex.setCodecStatusCode(ErrorCode::PROTOCOL_ERROR);
    // This will invoke sendAbort() and also inform the handler of the
    // error and detach the handler.
    onError(ex);
    return false;
  }
  return true;
}

void HTTPTransaction::onError(const HTTPException& error) {
  CallbackGuard guard(*this);

  const bool wasAborted = aborted_; // see comment below
  const bool wasEgressComplete = isEgressComplete();
  const bool wasIngressComplete = isIngressComplete();
  auto notify = handler_;

  if (error.getProxygenError() == kErrorStreamAbort) {
    DCHECK(error.getDirection() ==
           HTTPException::Direction::INGRESS_AND_EGRESS);
    aborted_ = true;
  } else if (error.hasCodecStatusCode()) {
    DCHECK(error.getDirection() ==
           HTTPException::Direction::INGRESS_AND_EGRESS);
    sendAbort(error.getCodecStatusCode());
  }

  switch (error.getDirection()) {
    case HTTPException::Direction::INGRESS_AND_EGRESS:
      markEgressComplete();
      markIngressComplete();
      if (wasEgressComplete && wasIngressComplete &&
          // We mark egress complete before we get acknowledgement of the
          // write segment finishing successfully.
          // TODO: instead of using CallbackGuard hacks to keep txn around, use
          // an explicit callback function and set egress complete after last
          // byte flushes (or egress error occurs), see #3912823
          (error.getProxygenError() != kErrorWriteTimeout || wasAborted)) {
        notify = nullptr;
      }
      break;
    case HTTPException::Direction::EGRESS:
      markEgressComplete();
      if (wasEgressComplete) {
        notify = nullptr;
      }
      break;
    case HTTPException::Direction::INGRESS:
      if (isIngressEOMSeen()) {
        // Not an error
        return;
      }
      markIngressComplete();
      if (wasIngressComplete) {
        notify = nullptr;
      }
      break;
  }
  if (notify) {
    notify->onError(error);
  }
}

void HTTPTransaction::onIngressTimeout() {
  CallbackGuard guard(*this);
  VLOG(4) << "ingress timeout on " << *this;
  pauseIngress();
  markIngressComplete();
  if (handler_) {
    HTTPException ex(HTTPException::Direction::INGRESS);
    ex.setProxygenError(kErrorTimeout);
    handler_->onError(ex);
  } else {
    markEgressComplete();
  }
}

void HTTPTransaction::onIngressWindowUpdate(const uint32_t amount) {
  if (!useFlowControl_) {
    return;
  }
  CallbackGuard guard(*this);
  VLOG(4) << *this << " Remote side ack'd " << amount << " bytes";
  updateReadTimeout();
  if (sendWindow_.free(amount)) {
    notifyTransportPendingEgress();
  } else {
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
    sendAbort(ErrorCode::FLOW_CONTROL_ERROR);
  }
}

void HTTPTransaction::onEgressTimeout() {
  CallbackGuard guard(*this);
  VLOG(4) << "egress timeout on " << *this;
  if (handler_) {
    HTTPException ex(HTTPException::Direction::EGRESS);
    ex.setProxygenError(kErrorTimeout);
    handler_->onError(ex);
  } else {
    markEgressComplete();
  }
}

void HTTPTransaction::onEgressHeaderFirstByte() {
  CallbackGuard guard(*this);
  if (transportCallback_) {
    transportCallback_->firstHeaderByteFlushed();
  }
}

void HTTPTransaction::onEgressBodyFirstByte() {
  CallbackGuard guard(*this);
  if (transportCallback_) {
    transportCallback_->firstByteFlushed();
  }
}

void HTTPTransaction::onEgressBodyLastByte() {
  CallbackGuard guard(*this);
  if (transportCallback_) {
    transportCallback_->lastByteFlushed();
  }
}

void HTTPTransaction::onEgressLastByteAck(std::chrono::milliseconds latency) {
  CallbackGuard guard(*this);
  if (transportCallback_) {
    transportCallback_->lastByteAcked(latency);
  }
}

void HTTPTransaction::sendHeaders(const HTTPMessage& headers) {
  CHECK(HTTPTransactionEgressSM::transit(
          egressState_, HTTPTransactionEgressSM::Event::sendHeaders));
  DCHECK(!isEgressComplete());
  if (isDownstream() && !isPushed()) {
    lastResponseStatus_ = headers.getStatusCode();
  }
  HTTPHeaderSize size;
  transport_.sendHeaders(this, headers, &size);
  if (transportCallback_) {
    transportCallback_->headerBytesGenerated(size);
  }
}

void HTTPTransaction::sendBody(std::unique_ptr<folly::IOBuf> body) {
  CHECK(HTTPTransactionEgressSM::transit(
      egressState_, HTTPTransactionEgressSM::Event::sendBody));
  deferredEgressBody_.append(std::move(body));
  notifyTransportPendingEgress();
}

bool HTTPTransaction::onWriteReady(const uint32_t maxEgress) {
  CallbackGuard guard(*this);
  DCHECK(isEnqueued());
  // this txn is being serviced so lower it's priority -> higher numerical value
  priority_ |= 0x2;
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
        !egressPaused_ && sendWindow > 0);

  const size_t bytesLeft = deferredEgressBody_.chainLength();
  size_t canSend = std::min<size_t>(sendWindow, bytesLeft);
  size_t curLen = 0;
  size_t nbytes = 0;
  bool willSendEOM = false;

  if (chunkHeaders_.empty()) {
    // limit this txn to egressBodySizeLimit_ at a time
    curLen = std::min<size_t>(canSend, egressBodySizeLimit_);
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
      canSend = std::min<size_t>(canSend, egressBodySizeLimit_);
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
        DCHECK(canSend == 0);
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
  updateHandlerPauseState();

  if (transportCallback_) {
    transportCallback_->bodyBytesGenerated(nbytes);
  }
  return nbytes;
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
  DCHECK(body);
  DCHECK(bodyLen > 0);
  size_t nbytes = 0;
  VLOG(4) << *this << " Sending " << bodyLen << " bytes of body. eom="
          << ((sendEom) ? "yes" : "no");
  if (useFlowControl_) {
    CHECK(sendWindow_.reserve(bodyLen));
    VLOG(4) << *this << " send_window is "
            << sendWindow_.getSize() << " / " << sendWindow_.getCapacity();
  }
  if (sendEom) {
    CHECK(HTTPTransactionEgressSM::transit(
            egressState_, HTTPTransactionEgressSM::Event::eomFlushed));
  }
  updateReadTimeout();
  nbytes = transport_.sendBody(this, std::move(body), sendEom);
  return nbytes;
}

void
HTTPTransaction::sendEOM() {
  CallbackGuard guard(*this);
  CHECK(HTTPTransactionEgressSM::transit(
      egressState_, HTTPTransactionEgressSM::Event::sendEOM));
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
        << "enqueued=" << enqueued_ << ", "
        << "chainLength=" << deferredEgressBody_.chainLength() << "]";
    }
  } else {
    VLOG(4) << "Queued egress EOM on " << *this;
    notifyTransportPendingEgress();
  }
  if (ingressPaused_ && !isIngressComplete()) {
    resumeIngress();
  }
}

void HTTPTransaction::sendAbort() {
  sendAbort(isUpstream() ? ErrorCode::CANCEL
                         : ErrorCode::PROTOCOL_ERROR);
}

void HTTPTransaction::sendAbort(ErrorCode statusCode) {
  CallbackGuard guard(*this);
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

void
HTTPTransaction::checkForCompletion() {
  DCHECK(callbackDepth_ == 0);
  if (deleting_) {
    return;
  }
  if (isEgressComplete() && isIngressComplete() && !isEnqueued()) {
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
    delete this;
  }
}

void HTTPTransaction::pauseIngress() {
  VLOG(4) << *this << " pauseIngress request";
  CallbackGuard guard(*this);
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
  CallbackGuard guard(*this);
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
    VLOG(4) << *this << " Processing deferred ingress callback of type " <<
      callback.getEvent();
    switch (callback.getEvent()) {
      case HTTPEvent::Type::MESSAGE_BEGIN:
        LOG(FATAL) << "unreachable";
        break;
      case HTTPEvent::Type::HEADERS_COMPLETE:
        processIngressHeadersComplete(std::move(callback.getHeaders()));
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
  CallbackGuard guard(*this);
  if (egressPaused_) {
    VLOG(4) << *this << " egress already paused";
    return;
  }
  egressPaused_ = true;
  notifyTransportPendingEgress();
}

void HTTPTransaction::resumeEgress() {
  VLOG(4) << *this << " asked to resume egress";
  CallbackGuard guard(*this);
  if (!egressPaused_) {
    VLOG(4) << *this << " egress already not paused";
    return;
  }
  egressPaused_ = false;
  notifyTransportPendingEgress();
}

void HTTPTransaction::notifyTransportPendingEgress() {
  if (!egressPaused_ &&
      (deferredEgressBody_.chainLength() > 0 ||
       isEgressEOMQueued()) &&
      (!useFlowControl_ || sendWindow_.getSize() > 0)) {
    if (isEnqueued()) {
      // We're already in the queue, jiggle our priority
      priority_ ^= 0x3;
      egressQueue_.update(queueHandle_);
    } else {
      // Insert into the queue and let the session know we've got something
      queueHandle_ = egressQueue_.push(this);
      enqueued_ = true;
      transport_.notifyPendingEgress();
    }
  } else if (isEnqueued()) {
    dequeue();
  }
  updateHandlerPauseState();
}

void HTTPTransaction::updateHandlerPauseState() {
  bool handlerShouldBePaused = egressPaused_ ||
    (useFlowControl_ && sendWindow_.getSize() <= 0) ||
    (deferredEgressBody_.chainLength() >= egressBufferLimit_);
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
  CallbackGuard guard(*this);
  CHECK(pushTxn->assocStreamId_ == id_);
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

void
HTTPTransaction::describe(std::ostream& os) const {
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
  if (!recvWindow_.setCapacity(capacity)) {
    return;
  }
  recvToAck_ += delta;
}

std::ostream&
operator<<(std::ostream& os, const HTTPTransaction& txn) {
  txn.describe(os);
  return os;
}

} // proxygen
