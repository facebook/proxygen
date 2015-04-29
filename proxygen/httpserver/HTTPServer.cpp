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

#include <folly/ThreadName.h>
#include <folly/io/async/EventBaseManager.h>
#include <proxygen/httpserver/HTTPServerAcceptor.h>
#include <proxygen/httpserver/SignalHandler.h>
#include <proxygen/httpserver/filters/RejectConnectFilter.h>

using folly::AsyncServerSocket;
using folly::EventBase;
using folly::EventBaseManager;
using folly::SocketAddress;
using folly::wangle::IOThreadPoolExecutor;
using folly::wangle::ThreadPoolExecutor;

namespace proxygen {

class AcceptorFactory : public folly::AcceptorFactory {
 public:
  AcceptorFactory(std::shared_ptr<HTTPServerOptions> options,
                  AcceptorConfiguration config) :
      options_(options),
      config_(config)  {}
  virtual std::shared_ptr<folly::Acceptor> newAcceptor(
      folly::EventBase* eventBase) {
    auto acc = std::shared_ptr<HTTPServerAcceptor>(
      HTTPServerAcceptor::make(config_, *options_).release());
    acc->init(nullptr, eventBase);
    return acc;
  }

 private:
  std::shared_ptr<HTTPServerOptions> options_;
  AcceptorConfiguration config_;
};

HTTPServer::HTTPServer(HTTPServerOptions options):
    options_(std::make_shared<HTTPServerOptions>(std::move(options))) {

  // Insert a filter to fail all the CONNECT request, if required
  if (!options_->supportsConnect) {
    options_->handlerFactories.insert(
        options_->handlerFactories.begin(),
        folly::make_unique<RejectConnectFilterFactory>());
  }
}

HTTPServer::~HTTPServer() {
  CHECK(!mainEventBase_) << "Forgot to stop() server?";
}

void HTTPServer::bind(std::vector<IPConfig>& addrs) {
  addresses_ = addrs;
}

class HandlerCallbacks : public ThreadPoolExecutor::Observer {
 public:
  explicit HandlerCallbacks(std::shared_ptr<HTTPServerOptions> options) : options_(options) {}

  void threadStarted(ThreadPoolExecutor::ThreadHandle* h) {
    IOThreadPoolExecutor::getEventBase(h)->runInEventBaseThread([&](){
      for (auto& factory: options_->handlerFactories) {
        factory->onServerStart();
      }
    });
  }
  void threadStopped(ThreadPoolExecutor::ThreadHandle* h) {
    IOThreadPoolExecutor::getEventBase(h)->runInEventBaseThread([&](){
      for (auto& factory: options_->handlerFactories) {
        factory->onServerStop();
      }
    });
  }

 private:
  std::shared_ptr<HTTPServerOptions> options_;
};


void HTTPServer::start(std::function<void()> onSuccess,
                       std::function<void(std::exception_ptr)> onError) {
  mainEventBase_ = EventBaseManager::get()->getEventBase();

  auto accExe = std::make_shared<IOThreadPoolExecutor>(1);
  auto exe = std::make_shared<IOThreadPoolExecutor>(options_->threads);
  auto exeObserver = std::make_shared<HandlerCallbacks>(options_);

  try {
    FOR_EACH_RANGE (i, 0, addresses_.size()) {
      auto factory = std::make_shared<AcceptorFactory>(
        options_,
        HTTPServerAcceptor::makeConfig(addresses_[i], *options_));
      bootstrap_.push_back(folly::ServerBootstrap<folly::DefaultPipeline>());
      bootstrap_[i].childHandler(factory);
      bootstrap_[i].group(accExe, exe);
      bootstrap_[i].bind(addresses_[i].address);
    }
  } catch (const std::exception& ex) {
    stop();

    if (onError) {
      onError(std::current_exception());
      return;
    }

    throw;
  }

  // Install signal handler if required
  if (!options_->shutdownOn.empty()) {
    signalHandler_ = folly::make_unique<SignalHandler>(this);
    signalHandler_->install(options_->shutdownOn);
  }

  // Start the main event loop
  if (onSuccess) {
    onSuccess();
  }
  exe->addObserver(exeObserver);
  mainEventBase_->loopForever();
}

void HTTPServer::stop() {
  CHECK(mainEventBase_);

  for (auto& bootstrap : bootstrap_) {
    bootstrap.stop();
  }

  for (auto& bootstrap : bootstrap_) {
    bootstrap.join();
  }
  signalHandler_.reset();
  mainEventBase_->terminateLoopSoon();
  mainEventBase_ = nullptr;
}

}
