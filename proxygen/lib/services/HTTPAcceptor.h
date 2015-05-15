/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/wangle/acceptor/Acceptor.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <proxygen/lib/services/AcceptorConfiguration.h>
#include <proxygen/lib/utils/AsyncTimeoutSet.h>

namespace proxygen {

class HTTPAcceptor : public folly::Acceptor {
 public:
  explicit HTTPAcceptor(const AcceptorConfiguration& accConfig)
    : Acceptor(accConfig)
    , accConfig_(accConfig) {}

  /**
   * Returns true if this server is internal to facebook
   */
  bool isInternal() const {
    return accConfig_.internal;
  }

   /**
   * Access the general-purpose timeout manager for transactions.
   */
  virtual AsyncTimeoutSet* getTransactionTimeoutSet() {
    return transactionTimeouts_.get();
  }

  void init(folly::AsyncServerSocket* serverSocket,
            folly::EventBase* eventBase) override {
    Acceptor::init(serverSocket, eventBase);
    transactionTimeouts_.reset(new AsyncTimeoutSet(
                                 eventBase, accConfig_.transactionIdleTimeout));

  }

  const AcceptorConfiguration& getConfig() const { return accConfig_; }

 protected:
  AcceptorConfiguration accConfig_;
 private:
  AsyncTimeoutSet::UniquePtr transactionTimeouts_;
  AsyncTimeoutSet::UniquePtr tcpEventsTimeouts_;
};

}
