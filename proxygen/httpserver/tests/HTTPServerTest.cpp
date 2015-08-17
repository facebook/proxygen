/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <boost/thread.hpp>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/EventBaseManager.h>
#include <gtest/gtest.h>
#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <proxygen/lib/utils/TestUtils.h>

using namespace proxygen;
using namespace testing;

using folly::AsyncSSLSocket;
using folly::AsyncServerSocket;
using folly::EventBaseManager;
using folly::SSLContext;
using folly::SSLContext;
using folly::SocketAddress;

namespace {

const std::string kTestDir = getContainingDirectory(__FILE__).str();

}

class ServerThread {
 private:
  boost::barrier barrier_{2};
  std::thread t_;
  HTTPServer* server_{nullptr};

 public:

  explicit ServerThread(HTTPServer* server) : server_(server) {}
  ~ServerThread() {
    if (server_) {
      server_->stop();
    }
    t_.join();
  }

  bool start() {
    bool throws = false;
    t_ = std::thread([&] () {
        server_->start(
          [&] () {
            barrier_.wait();
          },
          [&] (std::exception_ptr ex) {
            throws = true;
            server_ = nullptr;
            barrier_.wait();
          });
      });
    barrier_.wait();
    return !throws;
  }
};

TEST(MultiBind, HandlesListenFailures) {
  SocketAddress addr("127.0.0.1", 0);

  auto evb = EventBaseManager::get()->getEventBase();
  AsyncServerSocket::UniquePtr socket(new AsyncServerSocket(evb));
  socket->bind(addr);

  // Get the ephemeral port
  socket->getAddress(&addr);
  int port = addr.getPort();

  std::vector<HTTPServer::IPConfig> ips = {
    {
      folly::SocketAddress("127.0.0.1", port),
      HTTPServer::Protocol::HTTP
    }
  };

  HTTPServerOptions options;
  options.threads = 4;

  auto server = folly::make_unique<HTTPServer>(std::move(options));

  // We have to bind both the sockets before listening on either
  server->bind(ips);

  // On kernel 2.6 trying to listen on a FD that another socket
  // has bound to fails. While in kernel 3.2 only when one socket tries
  // to listen on a FD that another socket is listening on fails.
  try {
    socket->listen(1024);
  } catch (const std::exception& ex) {
    return;
  }

  ServerThread st(server.get());
  EXPECT_FALSE(st.start());
}

TEST(SSL, SSLTest) {
  HTTPServer::IPConfig cfg{
    folly::SocketAddress("127.0.0.1", 0),
      HTTPServer::Protocol::HTTP};
  wangle::SSLContextConfig sslCfg;
  sslCfg.isDefault = true;
  sslCfg.setCertificate(
    kTestDir + "certs/test_cert1.pem",
    kTestDir + "certs/test_key1.pem",
    "");
  cfg.sslConfigs.push_back(sslCfg);

  HTTPServerOptions options;
  options.threads = 4;

  auto server = folly::make_unique<HTTPServer>(std::move(options));

  std::vector<HTTPServer::IPConfig> ips{cfg};
  server->bind(ips);

  ServerThread st(server.get());
  EXPECT_TRUE(st.start());

  // Make an SSL connection to the server
  class Cb : public folly::AsyncSocket::ConnectCallback {
   public:
    explicit Cb(folly::AsyncSSLSocket* sock) : sock_(sock) {}
    void connectSuccess() noexcept override {
      success = true;
      sock_->close();
    }
    void connectErr(const folly::AsyncSocketException&)
      noexcept override {
      success = false;
    }

    bool success{false};
    folly::AsyncSSLSocket* sock_{nullptr};
  };

  folly::EventBase evb;
  auto ctx = std::make_shared<SSLContext>();
  folly::AsyncSSLSocket::UniquePtr sock(new folly::AsyncSSLSocket(ctx, &evb));
  Cb cb(sock.get());
  sock->connect(&cb, server->addresses().front().address, 1000);
  evb.loop();
  EXPECT_TRUE(cb.success);
}
