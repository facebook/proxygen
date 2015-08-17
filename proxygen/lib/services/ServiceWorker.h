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

#include <wangle/acceptor/Acceptor.h>
#include <wangle/acceptor/ConnectionCounter.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <list>
#include <memory>

namespace proxygen {

class Service;
class RequestWorker;

/**
 * ServiceWorker contains all of the per-thread information for a Service.
 *
 * ServiceWorker contains a pointer back to the single global Service.
 * It contains a list of ProxyAcceptor objects for this worker thread,
 * one per VIP.
 *
 * ServiceWorker fits into the Proxygen object hierarchy as follows:
 *
 * - Service: one instance (globally across the entire program)
 * - ServiceWorker: one instance per thread
 * - ServiceAcceptor: one instance per VIP per thread
 */
class ServiceWorker {
 public:
  ServiceWorker(Service* service, RequestWorker* worker)
      : service_(service), worker_(worker) {
  }

  virtual ~ServiceWorker() {}

  Service* getService() const {
    return service_;
  }

  void addServiceAcceptor(std::unique_ptr<wangle::Acceptor> acceptor) {
    acceptors_.emplace_back(std::move(acceptor));
  }

  RequestWorker* getRequestWorker() const {
    return worker_;
  }

  const std::list<std::unique_ptr<wangle::Acceptor>>& getAcceptors() {
    return acceptors_;
  }

  // Flush any thread-local stats that the service is tracking
  virtual void flushStats() {
  }

  // Destruct all the acceptors
  void clearAcceptors() {
    acceptors_.clear();
  }

  wangle::IConnectionCounter* getConnectionCounter() {
    return &connectionCounter_;
  }

  virtual void forceStop() {}

 protected:
  wangle::SimpleConnectionCounter connectionCounter_;

 private:
  // Forbidden copy constructor and assignment operator
  ServiceWorker(ServiceWorker const &) = delete;
  ServiceWorker& operator=(ServiceWorker const &) = delete;

  /**
   * The global Service object.
   */
  Service* service_;

  /**
   * The RequestWorker that is actually responsible for running the EventBase
   * loop in this thread.
   */
  RequestWorker* worker_;

  /**
   * A list of the Acceptor objects specific to this worker thread, one
   * Acceptor per VIP.
   */
  std::list<std::unique_ptr<wangle::Acceptor>> acceptors_;
};

} // proxygen
