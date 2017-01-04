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
  typedef std::map<folly::SocketAddress, std::unique_ptr<wangle::Acceptor>>
      AcceptorMap;

  ServiceWorker(Service* service, RequestWorker* worker)
      : service_(service), worker_(worker) {
  }

  virtual ~ServiceWorker() {}

  Service* getService() const {
    return service_;
  }

  void addServiceAcceptor(const folly::SocketAddress& address,
                              std::unique_ptr<wangle::Acceptor> acceptor) {
    addAcceptor(address, std::move(acceptor), acceptors_);
  }

  void drainServiceAcceptor(const folly::SocketAddress& address) {
    // Move the old acceptor to drainingAcceptors_ if present
    const auto& it = acceptors_.find(address);
    if (it != acceptors_.end()) {
      addAcceptor(address, std::move(it->second), drainingAcceptors_);
      acceptors_.erase(it);
    }
  }

  RequestWorker* getRequestWorker() const {
    return worker_;
  }

  const AcceptorMap& getAcceptors() {
    return acceptors_;
  }

  const AcceptorMap& getDrainingAcceptors() {
    return drainingAcceptors_;
  }

  // Flush any thread-local stats that the service is tracking
  virtual void flushStats() {
  }

  // Destruct all the acceptors
  virtual void clearAcceptors() {
    acceptors_.clear();
    drainingAcceptors_.clear();
  }

  virtual void clearDrainingAcceptors() {
    drainingAcceptors_.clear();
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

  void addAcceptor(const folly::SocketAddress& address,
                   std::unique_ptr<wangle::Acceptor> acceptor,
                   AcceptorMap& acceptors) {
    CHECK(acceptors.find(address) == acceptors.end());
    acceptors.insert(std::make_pair(address, std::move(acceptor)));
  }

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
  AcceptorMap acceptors_;

  /**
   * A list of Acceptors that are being drained and will be deleted soon.
   */
  AcceptorMap drainingAcceptors_;
};

} // proxygen
