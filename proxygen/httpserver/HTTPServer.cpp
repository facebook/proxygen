/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/httpserver/HTTPServer.h>

#include <boost/thread.hpp>
#include <folly/ThreadName.h>
#include <folly/io/async/EventBaseManager.h>
#include <proxygen/httpserver/HTTPServerAcceptor.h>
#include <proxygen/httpserver/SignalHandler.h>
#include <proxygen/httpserver/filters/RejectConnectFilter.h>

using folly::AsyncServerSocket;
using folly::EventBase;
using folly::EventBaseManager;
using folly::SocketAddress;

namespace proxygen {

HTTPServer::HTTPServer(HTTPServerOptions options):
    options_(std::move(options)) {

  // Insert a filter to fail all the CONNECT request, if required
  if (!options_.supportsConnect) {
    options_.handlerFactories.insert(
        options_.handlerFactories.begin(),
        folly::make_unique<RejectConnectFilterFactory>());
  }
}

HTTPServer::~HTTPServer() {
  CHECK(!mainEventBase_) << "Forgot to stop() server?";
}

void HTTPServer::bind(std::vector<IPConfig>& addrs) {
  CHECK(serverSockets_.empty()) << "Server sockets are already bound";

  // Set a scope guard that brings us to unintialized state
  folly::ScopeGuard g = folly::makeGuard([&] {
    serverSockets_.clear();
  });

  auto evb = EventBaseManager::get()->getEventBase();
  for (auto& addr: addrs) {
    serverSockets_.emplace_back(new AsyncServerSocket(evb));
    serverSockets_.back()->bind(addr.address);

    // Use might have asked to register with some ephemeral port
    serverSockets_.back()->getAddress(&addr.address);
  }

  // If everything succeeded, dismiss the cleanup guard
  g.dismiss();
  addresses_ = addrs;
}

void HTTPServer::start(std::function<void()> onSuccess,
                       std::function<void(std::exception_ptr)> onError) {
  // Step 1: Check that server sockets are bound
  CHECK(!serverSockets_.empty()) << "Need to call `bind()` before `start()`";

  // Global Event base manager that will be used to create all the worker
  // threads eventbases
  auto manager = EventBaseManager::get();
  mainEventBase_ = manager->getEventBase();

  // Will be used to make sure that all the event loops are running
  // before we proceed
  boost::barrier barrier(2);

  // Step 2: Setup handler threads
  handlerThreads_ = std::vector<HandlerThread>(options_.threads);

  std::vector<AcceptorConfiguration> accConfigs;
  FOR_EACH_RANGE (i, 0, addresses_.size()) {
    accConfigs.emplace_back(HTTPServerAcceptor::makeConfig(addresses_[i],
                                                           options_));
  }

  for (auto& handlerThread: handlerThreads_) {
    handlerThread.thread = std::thread([&] () {
      folly::setThreadName("http-worker");
      handlerThread.eventBase = manager->getEventBase();
      barrier.wait();

      handlerThread.eventBase->loopForever();

      // Call loop() again to drain all the events
      handlerThread.eventBase->loop();
    });

    // Wait for eventbase pointer to be set
    barrier.wait();

    // Make sure event loop is running before we proceed
    handlerThread.eventBase->runInEventBaseThread([&] () {
      barrier.wait();
      for (auto& factory: options_.handlerFactories) {
        factory->onServerStart();
      }
    });
    barrier.wait();

    // Create acceptors
    FOR_EACH_RANGE (i, 0, accConfigs.size()) {
      auto acc = HTTPServerAcceptor::make(accConfigs[i], options_);
      acc->init(serverSockets_[i].get(), handlerThread.eventBase);
      ++handlerThread.acceptorsRunning;

      // Set completion callback such that it invokes onServerStop when
      // all acceptors are drained
      HandlerThread* ptr = &handlerThread;
      acc->setCompletionCallback([ptr, this] () {
        --ptr->acceptorsRunning;

        if (ptr->acceptorsRunning == 0) {
          for (auto& factory: options_.handlerFactories) {
            factory->onServerStop();
          }
        }
      });

      handlerThread.acceptors.push_back(std::move(acc));
    }
  }

  // Step 3: Install signal handler if required
  if (!options_.shutdownOn.empty()) {
    signalHandler_ = folly::make_unique<SignalHandler>(this);
    signalHandler_->install(options_.shutdownOn);
  }

  // Step 4: Switch the server socket eventbase (bind may have been invoked
  //         in a separate thread).
  for (auto& serverSocket: serverSockets_) {
    serverSocket->detachEventBase();
    serverSocket->attachEventBase(mainEventBase_);
  }

  // Step 5: Start listening for connections. This can throw if somebody else
  //         binds to same address and calls listen before this.
  try {
    for (auto& serverSocket: serverSockets_) {
      serverSocket->listen(options_.listenBacklog);
      serverSocket->startAccepting();
    }
  } catch (...) {
    stop();

    if (onError) {
      onError(std::current_exception());
      return;
    }

    throw;
  }

  // Step 6: Start the main event loop
  if (onSuccess) {
    mainEventBase_->runInEventBaseThread([onSuccess] () {
      onSuccess();
    });
  }

  mainEventBase_->loopForever();
}

void HTTPServer::stop() {
  CHECK(mainEventBase_);

  if (!mainEventBase_->isInEventBaseThread()) {
    auto barrier = std::make_shared<boost::barrier>(2);
    mainEventBase_->runInEventBaseThread([this, barrier] () {
      stop();
      barrier->wait();
    });

    barrier->wait();
    return;
  }

  signalHandler_.reset();
  serverSockets_.clear();
  mainEventBase_->terminateLoopSoon();
  mainEventBase_ = nullptr;

  for (auto& handlerThread: handlerThreads_) {
    handlerThread.eventBase->terminateLoopSoon();
  }

  for (auto& handlerThread: handlerThreads_) {
    handlerThread.thread.join();
  }

  handlerThreads_.clear();
}

}
