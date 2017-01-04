/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <wangle/acceptor/Acceptor.h>
#include <proxygen/lib/services/AcceptorConfiguration.h>
#include <proxygen/lib/utils/AsyncTimeoutSet.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <proxygen/lib/utils/WheelTimerInstance.h>

namespace proxygen {

class HTTPAcceptor : public wangle::Acceptor {
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
  virtual const WheelTimerInstance& getTransactionTimeoutSet() {
    return *timer_;
  }

  void init(folly::AsyncServerSocket* serverSocket,
            folly::EventBase* eventBase,
            wangle::SSLStats* stat=nullptr) override {
    timer_ = folly::make_unique<WheelTimerInstance>(
        accConfig_.transactionIdleTimeout, eventBase);
    Acceptor::init(serverSocket, eventBase);
  }

  const AcceptorConfiguration& getConfig() const { return accConfig_; }

 protected:
  AcceptorConfiguration accConfig_;
 private:
  AsyncTimeoutSet::UniquePtr tcpEventsTimeouts_;
  std::unique_ptr<WheelTimerInstance> timer_;
};

}
