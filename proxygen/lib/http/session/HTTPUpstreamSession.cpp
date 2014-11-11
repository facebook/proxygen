/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>

#include <proxygen/lib/http/session/HTTPTransaction.h>

#include <boost/cast.hpp>
#include <folly/experimental/wangle/ConnectionManager.h>

namespace proxygen {

HTTPUpstreamSession::~HTTPUpstreamSession() {}

bool HTTPUpstreamSession::isReusable() const {
  VLOG(4) << "isReusable: " << *this
    << ", liveTransactions_=" << liveTransactions_
    << ", sock_->good()=" << sock_->good()
    << ", sock_->connecting()=" << sock_->connecting()
    << ", draining_=" << draining_
    << ", codec_->isReusable()=" << codec_->isReusable()
    << ", codec_->isBusy()=" << codec_->isBusy()
    << ", pendingWriteSize_=" << pendingWriteSize_
    << ", numActiveWrites_=" << numActiveWrites_
    << ", writeTimeout_.isScheduled()=" << writeTimeout_.isScheduled()
    << ", readsShutdown_=" << readsShutdown_
    << ", writesShutdown_=" << writesShutdown_
    << ", writesDraining_=" << writesDraining_
    << ", resetAfterDrainingWrites_=" << resetAfterDrainingWrites_
    << ", ingressError_=" << ingressError_
    << ", hasMoreWrites()=" << hasMoreWrites()
    << ", codec_->supportsParallelRequests()="
         << codec_->supportsParallelRequests();
  return
    sock_->good() &&
    !sock_->connecting() &&
    codec_->isReusable() &&
    !draining_ &&
    !codec_->isBusy() &&
    !readsShutdown_ &&
    !writesShutdown_ &&
    !writesDraining_ &&
    !resetAfterDrainingWrites_ &&
    !ingressError_ &&
    (codec_->supportsParallelRequests() || (
      // These conditions only apply to serial codec sessions
      !hasMoreWrites() &&
      liveTransactions_ == 0 &&
      !writeTimeout_.isScheduled()));
}

HTTPTransaction*
HTTPUpstreamSession::newTransaction(HTTPTransaction::Handler* handler,
                                    int8_t priority) {
  CHECK_NOTNULL(handler);

  if (!supportsMoreTransactions() || draining_) {
    // This session doesn't support any more parallel transactions
    return nullptr;
  }

  if (!started_) {
    startNow();
  }

  HTTPCodec::StreamID streamID = codec_->createStream();
  HTTPTransaction* txn = new HTTPTransaction(
    codec_->getTransportDirection(), streamID, transactionSeqNo_, *this,
    txnEgressQueue_, transactionTimeouts_, sessionStats_,
    codec_->supportsStreamFlowControl(),
    initialReceiveWindow_,
    getCodecSendWindowSize(),
    priority);

  if (!addTransaction(txn)) {
    delete txn;
    return nullptr;
  }

  transactionSeqNo_++;

  txn->setReceiveWindow(receiveStreamWindowSize_);
  txn->setHandler(handler);
  setNewTransactionPauseState(txn);
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
  for (const auto txn: transactions_) {
    if (!txn.second->isPushed() && !txn.second->isEgressStarted()) {
      return false;
    }
  }
  return true;
}

} // proxygen
