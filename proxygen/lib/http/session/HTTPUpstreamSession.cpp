/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <proxygen/lib/http/session/HTTPSessionController.h>

#include <wangle/acceptor/ConnectionManager.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/http/codec/HTTPCodecFactory.h>

namespace proxygen {

HTTPUpstreamSession::~HTTPUpstreamSession() {}

bool HTTPUpstreamSession::isReusable() const {
  VLOG(4) << "isReusable: " << *this
    << ", liveTransactions_=" << liveTransactions_
    << ", isClosing()=" << isClosing()
    << ", sock_->connecting()=" << sock_->connecting()
    << ", codec_->isReusable()=" << codec_->isReusable()
    << ", codec_->isBusy()=" << codec_->isBusy()
    << ", pendingWriteSize_=" << pendingWriteSize_
    << ", numActiveWrites_=" << numActiveWrites_
    << ", writeTimeout_.isScheduled()=" << writeTimeout_.isScheduled()
    << ", ingressError_=" << ingressError_
    << ", hasMoreWrites()=" << hasMoreWrites()
    << ", codec_->supportsParallelRequests()="
         << codec_->supportsParallelRequests();
  return
    !isClosing() &&
    !sock_->connecting() &&
    codec_->isReusable() &&
    !codec_->isBusy() &&
    !ingressError_ &&
    (codec_->supportsParallelRequests() || (
      // These conditions only apply to serial codec sessions
      !hasMoreWrites() &&
      liveTransactions_ == 0 &&
      !writeTimeout_.isScheduled()));
}

bool HTTPUpstreamSession::isClosing() const {
  VLOG(5) << "isClosing: " << *this
    << ", sock_->good()=" << sock_->good()
    << ", draining_=" << draining_
    << ", readsShutdown()=" << readsShutdown()
    << ", writesShutdown()=" << writesShutdown()
    << ", writesDraining_=" << writesDraining_
    << ", resetAfterDrainingWrites_=" << resetAfterDrainingWrites_;
  return
    !sock_->good() ||
    draining_ ||
    readsShutdown() ||
    writesShutdown() ||
    writesDraining_ ||
    resetAfterDrainingWrites_;
}

void HTTPUpstreamSession::startNow() {
  // startNow in base class CHECKs this session has not started.
  HTTPSession::startNow();
  // Upstream specific:
  // create virtual priority nodes and send Priority frames to peer if necessary
  auto bytes = codec_->addPriorityNodes(
      txnEgressQueue_,
      writeBuf_,
      maxVirtualPriorityLevel_);
  if (bytes) {
    scheduleWrite();
  }
}

HTTPTransaction*
HTTPUpstreamSession::newTransaction(HTTPTransaction::Handler* handler) {
  if (!supportsMoreTransactions() || draining_) {
    // This session doesn't support any more parallel transactions
    return nullptr;
  }

  if (!started_) {
    startNow();
  }

  auto txn = createTransaction(codec_->createStream(), 0);

  if (txn) {
    DestructorGuard dg(this);
    auto txnID = txn->getID();
    txn->setHandler(CHECK_NOTNULL(handler));
    setNewTransactionPauseState(txnID);
  }
  return txn;
}

HTTPTransaction::Handler*
HTTPUpstreamSession::getParseErrorHandler(HTTPTransaction* txn,
                                          const HTTPException& error) {
  // No special handler for upstream requests that have a parse error
  return nullptr;
}

HTTPTransaction::Handler*
HTTPUpstreamSession::getTransactionTimeoutHandler(HTTPTransaction* txn) {
  // No special handler for upstream requests that time out
  return nullptr;
}

bool HTTPUpstreamSession::allTransactionsStarted() const {
  for (const auto& txn: transactions_) {
    if (!txn.second.isPushed() && !txn.second.isEgressStarted()) {
      return false;
    }
  }
  return true;
}

bool HTTPUpstreamSession::onNativeProtocolUpgrade(
  HTTPCodec::StreamID streamID, CodecProtocol protocol,
  const std::string& protocolString,
  HTTPMessage&) {

  VLOG(4) << *this << " onNativeProtocolUpgrade streamID=" << streamID <<
    " protocol=" << protocolString;

  // Create the new Codec
  auto codec = HTTPCodecFactory::getCodec(protocol,
                                          TransportDirection::UPSTREAM);
  bool ret = onNativeProtocolUpgradeImpl(streamID, std::move(codec),
                                         protocolString);
  if (ret) {
    auto bytes = codec_->addPriorityNodes(
      txnEgressQueue_,
      writeBuf_,
      maxVirtualPriorityLevel_);
    if (bytes) {
      scheduleWrite();
    }
  }
  return ret;
}

void
HTTPUpstreamSession::attachThreadLocals(
  folly::EventBase* eventBase,
  folly::SSLContextPtr sslContext,
  const WheelTimerInstance& timeout,
  HTTPSessionStats* stats, FilterIteratorFn fn,
  HeaderCodec::Stats* headerCodecStats,
  HTTPUpstreamSessionController* controller) {
  txnEgressQueue_.attachThreadLocals(timeout);
  timeout_ = timeout;
  setController(controller);
  setSessionStats(stats);
  if (sock_) {
    sock_->attachEventBase(eventBase);
    auto sslSocket = sock_->getUnderlyingTransport<folly::AsyncSSLSocket>();
    if (sslSocket) {
      sslSocket->attachSSLContext(sslContext);
    }
  }
  codec_.foreach(fn);
  codec_->setHeaderCodecStats(headerCodecStats);
  resumeReadsImpl();
  rescheduleLoopCallbacks();
}

void
HTTPUpstreamSession::detachThreadLocals() {
  CHECK(transactions_.empty());
  cancelLoopCallbacks();
  pauseReadsImpl();
  if (sock_) {
    auto sslSocket = sock_->getUnderlyingTransport<folly::AsyncSSLSocket>();
    if (sslSocket) {
      sslSocket->detachSSLContext();
    }
    sock_->detachEventBase();
  }
  txnEgressQueue_.detachThreadLocals();
  setController(nullptr);
  setSessionStats(nullptr);
  // The codec filters *shouldn't* be accessible while the socket is detached,
  // I hope
  codec_->setHeaderCodecStats(nullptr);
  auto cm = getConnectionManager();
  if (cm) {
    cm->removeConnection(this);
  }
}


} // proxygen
