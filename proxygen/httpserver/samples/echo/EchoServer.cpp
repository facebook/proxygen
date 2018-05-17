/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Memory.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/Unistd.h>
#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>

#include "EchoHandler.h"
#include "EchoStats.h"

using namespace EchoService;
using namespace proxygen;
using namespace folly;

using folly::EventBase;
using folly::EventBaseManager;
using folly::SocketAddress;

using Protocol = HTTPServer::Protocol;

DEFINE_int32(http_port, 11000, "Port to listen on with HTTP protocol");
DEFINE_int32(spdy_port, 11001, "Port to listen on with SPDY protocol");
DEFINE_int32(h2_port, 11002, "Port to listen on with HTTP/2 protocol");
DEFINE_string(ip, "localhost", "IP/Hostname to bind to");
DEFINE_int32(threads, 1, "Number of threads to listen on. Numbers <= 0 "
             "will use the number of cores on this machine.");

std::atomic<int> numThreads;
std::shared_ptr<IOThreadPoolExecutor> iotpe_ = nullptr;

class EchoHandlerFactory : public RequestHandlerFactory {
 public:
  void onServerStart(folly::EventBase* /*evb*/) noexcept override {
    stats_.reset(new EchoStats);
  }

  void onServerStop() noexcept override {
    stats_.reset();
  }

  RequestHandler* onRequest(RequestHandler*, HTTPMessage*) noexcept override {
    numThreads++;
    iotpe_->setNumThreads(numThreads.load());
    return new EchoHandler(stats_.get());
  }

 private:
  folly::ThreadLocalPtr<EchoStats> stats_;
};


class IOCallbacks : public ThreadPoolExecutor::Observer {
 public:
  explicit IOCallbacks(std::shared_ptr<HTTPServerOptions> options) : options_(options) {}

  void threadStarted(ThreadPoolExecutor::ThreadHandle* h) override {
    auto evb = IOThreadPoolExecutor::getEventBase(h);
    evb->runInEventBaseThread([=](){
		std::cerr <<"Started Thread :" << std::this_thread::get_id() <<std::endl;
    }); 
  }
  void threadStopped(ThreadPoolExecutor::ThreadHandle* h) override {
    IOThreadPoolExecutor::getEventBase(h)->runInEventBaseThread([&](){
		std::cerr <<"Stopped Thread :" << std::this_thread::get_id() <<std::endl;
    }); 
  }

 private:
  std::shared_ptr<HTTPServerOptions> options_;
};



int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  numThreads.store(0);

  std::vector<HTTPServer::IPConfig> IPs = {
    {SocketAddress(FLAGS_ip, FLAGS_http_port, true), Protocol::HTTP},
    {SocketAddress(FLAGS_ip, FLAGS_spdy_port, true), Protocol::SPDY},
    {SocketAddress(FLAGS_ip, FLAGS_h2_port, true), Protocol::HTTP2},
  };

  if (FLAGS_threads <= 0) {
    FLAGS_threads = sysconf(_SC_NPROCESSORS_ONLN);
    CHECK(FLAGS_threads > 0);
  }

  HTTPServerOptions options;
  options.threads = static_cast<size_t>(FLAGS_threads);
  options.idleTimeout = std::chrono::milliseconds(60000);
  options.shutdownOn = {SIGINT, SIGTERM};
  options.enableContentCompression = false;
  options.handlerFactories = RequestHandlerChain()
      .addThen<EchoHandlerFactory>()
      .build();
  options.h2cEnabled = true;

  HTTPServer server(std::move(options));
  server.bind(IPs);

  std::shared_ptr<ThreadPoolExecutor::Observer> obs_ = std::make_shared<IOCallbacks>(nullptr);

  std::function<void()>  onSuccessCallBack = ([&] () {
    iotpe_ = server.getIOThreadPoolExecutor();
    folly::ThreadPoolExecutor::PoolStats stats = iotpe_->getPoolStats();
    std::cout <<"Started http server , num threads in thread pool :" << stats.threadCount <<std::endl;
    iotpe_->addObserver(obs_);
  });

  std::function<void(std::exception_ptr)>  onFailureCallBack = ([]  (std::exception_ptr) {
    std::cout <<"Failed to start http server\n";
  });


  // Start HTTPServer mainloop in a separate thread
  std::thread t([&] () {
    server.start(onSuccessCallBack, onFailureCallBack);
  });

  t.join();

  return 0;
}
