/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPDownstreamSession.h>

#include <proxygen/lib/http/session/HTTPSessionController.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>

namespace proxygen {

HTTPDownstreamSession::~HTTPDownstreamSession() {
}

void
HTTPDownstreamSession::setupOnHeadersComplete(HTTPTransaction* txn,
                                              HTTPMessage* msg) {
  CHECK(!txn->getHandler());

  // We need to find a Handler to process the transaction.
  // Note: The handler is responsible for freeing itself
  // when it has finished processing the transaction.  The
  // transaction is responsible for freeing itself when both the
  // ingress and egress messages have completed (or failed).
  HTTPTransaction::Handler* handler = nullptr;

  // In the general case, delegate to the handler factory to generate
  // a handler for the transaction.
  handler = controller_->getRequestHandler(*txn, msg);
  CHECK(handler);

  DestructorGuard dg(this);
  auto txnID = txn->getID();
  txn->setHandler(handler);
  setNewTransactionPauseState(txnID);
}

HTTPTransaction::Handler*
HTTPDownstreamSession::getParseErrorHandler(HTTPTransaction* txn,
                                            const HTTPException& error) {
  // we encounter an error before we finish reading the ingress headers.
  return controller_->getParseErrorHandler(txn, error, localAddr_);
}

HTTPTransaction::Handler*
HTTPDownstreamSession::getTransactionTimeoutHandler(
  HTTPTransaction* txn) {
  return controller_->getTransactionTimeoutHandler(txn, localAddr_);
}

void
HTTPDownstreamSession::onHeadersSent(const HTTPMessage& headers,
                                     bool codecWasReusable) {
  if (!codec_->isReusable()) {
    // If the codec turned unreusable, some thing wrong must have happened.
    // Basically, the proxy decides the connection is not reusable.
    // e.g, an error message is being sent with Connection: close
    if (codecWasReusable) {
      uint32_t statusCode = headers.getStatusCode();
      if (statusCode >= 500) {
        setCloseReason(ConnectionCloseReason::REMOTE_ERROR);
      } else {
        if (statusCode >= 400) {
          setCloseReason(ConnectionCloseReason::ERR_RESP);
        } else {
          // should not be here
          setCloseReason(ConnectionCloseReason::UNKNOWN);
        }
      }
    } else {
      // shouldn't happen... this case is detected by REQ_NOTREUSABLE
      setCloseReason(ConnectionCloseReason::UNKNOWN);
    }
  }
}

bool HTTPDownstreamSession::allTransactionsStarted() const {
  for (const auto& txn: transactions_) {
    if (txn.second.isPushed() && !txn.second.isEgressStarted()) {
      return false;
    }
  }
  return true;
}

} // proxygen
