/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "proxygen/lib/services/Acceptor.h"
#include "proxygen/lib/services/AcceptorConfiguration.h"

namespace proxygen {

class HTTPAcceptor : public Acceptor {
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
  virtual apache::thrift::async::TAsyncTimeoutSet* getTransactionTimeoutSet() {
    return transactionTimeouts_.get();
  }

  virtual void init(apache::thrift::async::TAsyncServerSocket* serverSocket,
                    folly::EventBase* eventBase) {
    Acceptor::init(serverSocket, eventBase);
    transactionTimeouts_.reset(new apache::thrift::async::TAsyncTimeoutSet(
                                 eventBase, accConfig_.transactionIdleTimeout));

  }

  const AcceptorConfiguration& getConfig() const { return accConfig_; }

 protected:
  AcceptorConfiguration accConfig_;
 private:
  apache::thrift::async::TAsyncTimeoutSet::UniquePtr transactionTimeouts_;
  apache::thrift::async::TAsyncTimeoutSet::UniquePtr tcpEventsTimeouts_;
};

}
