/*
 *  Copyright (c) 2017, Facebook, Inc.
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
#include <proxygen/httpserver/filters/ZlibServerFilter.h>
#include <wangle/concurrent/NamedThreadFactory.h>
#include <wangle/ssl/SSLContextManager.h>

using folly::AsyncServerSocket;
using folly::EventBase;
using folly::EventBaseManager;
using folly::SocketAddress;
using wangle::IOThreadPoolExecutor;
using wangle::ThreadPoolExecutor;

namespace proxygen {

class AcceptorFactory : public wangle::AcceptorFactory {
 public:
  AcceptorFactory(std::shared_ptr<HTTPServerOptions> options,
                  std::shared_ptr<HTTPCodecFactory> codecFactory,
                  AcceptorConfiguration config,
                  HTTPSession::InfoCallback* sessionInfoCb) :
      options_(options),
      codecFactory_(codecFactory),
      config_(config),
      sessionInfoCb_(sessionInfoCb) {}
  std::shared_ptr<wangle::Acceptor> newAcceptor(
      folly::EventBase* eventBase) override {
    auto acc = std::shared_ptr<HTTPServerAcceptor>(
      HTTPServerAcceptor::make(config_, *options_, codecFactory_).release());
    if (sessionInfoCb_) {
      acc->setSessionInfoCallback(sessionInfoCb_);
    }
    acc->init(nullptr, eventBase);
    return acc;
  }

 private:
  std::shared_ptr<HTTPServerOptions> options_;
  std::shared_ptr<HTTPCodecFactory> codecFactory_;
  AcceptorConfiguration config_;
  HTTPSession::InfoCallback* sessionInfoCb_;
};

HTTPServer::HTTPServer(HTTPServerOptions options):
    options_(std::make_shared<HTTPServerOptions>(std::move(options))) {

  // Insert a filter to fail all the CONNECT request, if required
  if (!options_->supportsConnect) {
    options_->handlerFactories.insert(
        options_->handlerFactories.begin(),
        folly::make_unique<RejectConnectFilterFactory>());
  }

  // Add Content Compression filter (gzip), if needed. Should be
  // final filter
  if (options_->enableContentCompression) {
    options_->handlerFactories.insert(
        options_->handlerFactories.begin(),
        folly::make_unique<ZlibServerFilterFactory>(
          options_->contentCompressionLevel,
          options_->contentCompressionMinimumSize,
          options_->contentCompressionTypes));
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

  void threadStarted(ThreadPoolExecutor::ThreadHandle* h) override {
    auto evb = IOThreadPoolExecutor::getEventBase(h);
    evb->runInEventBaseThread([=](){
      for (auto& factory: options_->handlerFactories) {
        factory->onServerStart(evb);
      }
    });
  }
  void threadStopped(ThreadPoolExecutor::ThreadHandle* h) override {
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
  auto exe = std::make_shared<IOThreadPoolExecutor>(options_->threads,
    std::make_shared<wangle::NamedThreadFactory>("HTTPSrvExec"));
  auto exeObserver = std::make_shared<HandlerCallbacks>(options_);
  // Observer has to be set before bind(), so onServerStart() callbacks run
  exe->addObserver(exeObserver);

  try {
    FOR_EACH_RANGE (i, 0, addresses_.size()) {
      auto codecFactory = addresses_[i].codecFactory;
      auto accConfig = HTTPServerAcceptor::makeConfig(addresses_[i], *options_);
      auto factory = std::make_shared<AcceptorFactory>(
        options_,
        codecFactory,
        accConfig,
        sessionInfoCb_);
      bootstrap_.push_back(
          wangle::ServerBootstrap<wangle::DefaultPipeline>());
      bootstrap_[i].childHandler(factory);
      if (accConfig.enableTCPFastOpen) {
        // We need to do this because wangle's bootstrap has 2 acceptor configs
        // and the socketConfig gets passed to the SocketFactory. The number of
        // configs should really be one, and when that happens, we can remove
        // this code path.
        bootstrap_[i].socketConfig.enableTCPFastOpen = true;
        bootstrap_[i].socketConfig.fastOpenQueueSize =
            accConfig.fastOpenQueueSize;
      }
      bootstrap_[i].group(accExe, exe);
      if (options_->preboundSockets_.size() > 0) {
        bootstrap_[i].bind(std::move(options_->preboundSockets_[i]));
      } else {
        bootstrap_[i].bind(addresses_[i].address);
      }
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

  // Start the main event loop.
  if (onSuccess) {
    mainEventBase_->runInLoop([onSuccess(std::move(onSuccess))]() {
      // IMPORTANT: Since we may be racing with stop(), we must assume that
      // mainEventBase_ can become null the moment that onSuccess is called,
      // so this **has** to be queued to run from inside loopForever().
        onSuccess();
    });
  }
  mainEventBase_->loopForever();
}

void HTTPServer::stopListening() {
  CHECK(mainEventBase_);
  for (auto& bootstrap : bootstrap_) {
    bootstrap.stop();
  }
}

void HTTPServer::stop() {
  stopListening();

  for (auto& bootstrap : bootstrap_) {
    bootstrap.join();
  }

  signalHandler_.reset();
  mainEventBase_->terminateLoopSoon();
  mainEventBase_ = nullptr;
}

const std::vector<const folly::AsyncSocketBase*>
  HTTPServer::getSockets() const {

  std::vector<const folly::AsyncSocketBase*> sockets;
  FOR_EACH_RANGE(i, 0, bootstrap_.size()) {
    auto& bootstrapSockets = bootstrap_[i].getSockets();
    FOR_EACH_RANGE(j, 0, bootstrapSockets.size()) {
      sockets.push_back(bootstrapSockets[j].get());
    }
  }

  return sockets;
}

int HTTPServer::getListenSocket() const {
  if (bootstrap_.size() == 0) {
    return -1;
  }

  auto& bootstrapSockets = bootstrap_[0].getSockets();
  if (bootstrapSockets.size() == 0) {
    return -1;
  }

  auto serverSocket =
      std::dynamic_pointer_cast<folly::AsyncServerSocket>(bootstrapSockets[0]);
  auto socketFds = serverSocket->getSockets();
  if (socketFds.size() == 0) {
    return -1;
  }

  return socketFds[0];
}

void HTTPServer::updateTicketSeeds(wangle::TLSTicketKeySeeds seeds) {
  for (auto& bootstrap : bootstrap_) {
    bootstrap.forEachWorker([&](wangle::Acceptor* acceptor) {
      if (!acceptor) {
        return;
      }
      auto evb = acceptor->getEventBase();
      if (!evb) {
        return;
      }
      evb->runInEventBaseThread([acceptor, seeds] {
        acceptor->setTLSTicketSecrets(
            seeds.oldSeeds, seeds.currentSeeds, seeds.newSeeds);
      });
    });
  }
}

}
