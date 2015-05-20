/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>

#include <folly/wangle/acceptor/ConnectionManager.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>

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

  auto txn = createTransaction(codec_->createStream(),
                               0,
                               priority);

  if (txn) {
    DestructorGuard dg(this);
    auto txnID = txn->getID();
    txn->setHandler(handler);
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

} // proxygen
