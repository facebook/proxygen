/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPSessionAcceptor.h>
#include <proxygen/lib/http/session/test/HTTPSessionMocks.h>
#include <proxygen/lib/utils/TestUtils.h>
#include <folly/io/async/test/MockAsyncServerSocket.h>
#include <folly/io/async/test/MockAsyncSocket.h>

using namespace proxygen;
using namespace testing;

using folly::AsyncSocket;
using folly::test::MockAsyncSocket;
using folly::SocketAddress;

namespace {

const std::string kTestDir = getContainingDirectory(__FILE__).str();

}

class HTTPTargetSessionAcceptor : public HTTPSessionAcceptor {
 public:
  explicit HTTPTargetSessionAcceptor(const AcceptorConfiguration& accConfig)
  : HTTPSessionAcceptor(accConfig) {
  }

  HTTPTransaction::Handler* newHandler(HTTPTransaction& txn,
                                       HTTPMessage* msg) noexcept override {
    return new MockHTTPHandler();
  }

  void onCreate(const HTTPSession& session) override{
    EXPECT_EQ(expectedProto_,
              getCodecProtocolString(session.getCodec().getProtocol()));
    sessionsCreated_++;
  }

  void connectionReady(AsyncSocket::UniquePtr sock,
                       const SocketAddress& clientAddr,
                       const std::string& nextProtocolName,
                       folly::TransportInfo& tinfo) {
    HTTPSessionAcceptor::connectionReady(std::move(sock),
                                         clientAddr,
                                         nextProtocolName,
                                         tinfo);
  }

  uint32_t sessionsCreated_{0};
  std::string expectedProto_;
};

class HTTPSessionAcceptorTestBase :
    public ::testing::TestWithParam<const char*> {
 public:

  virtual void setupSSL() {
    sslCtxConfig_.setCertificate(
      kTestDir + "test_cert1.pem",
      kTestDir + "test_cert1.key",
      "");

    sslCtxConfig_.isDefault = true;
    config_.sslContextConfigs.emplace_back(sslCtxConfig_);
  }

  void SetUp() override {
    SocketAddress address("127.0.0.1", 0);
    config_.bindAddress = address;
    setupSSL();
    newAcceptor();
  }

  void newAcceptor() {
    acceptor_ = folly::make_unique<HTTPTargetSessionAcceptor>(config_);
    EXPECT_CALL(mockServerSocket_, addAcceptCallback(_, _, _));
    acceptor_->init(&mockServerSocket_, &eventBase_);
  }

 protected:
  AcceptorConfiguration config_;
  folly::SSLContextConfig sslCtxConfig_;
  std::unique_ptr<HTTPTargetSessionAcceptor> acceptor_;
  folly::EventBase eventBase_;
  folly::test::MockAsyncServerSocket mockServerSocket_;
};

class HTTPSessionAcceptorTestNPN :
    public HTTPSessionAcceptorTestBase {};
class HTTPSessionAcceptorTestNPNPlaintext :
    public HTTPSessionAcceptorTestBase {
 public:
  void setupSSL() override {}
};
class HTTPSessionAcceptorTestNPNJunk :
    public HTTPSessionAcceptorTestBase {};

// Verify HTTPSessionAcceptor creates the correct codec based on NPN
TEST_P(HTTPSessionAcceptorTestNPN, npn) {
  std::string proto(GetParam());
  if (proto == "") {
    acceptor_->expectedProto_ = "http/1.1";
  } else {
    acceptor_->expectedProto_ = proto;
  }

  AsyncSocket::UniquePtr sock(new AsyncSocket(&eventBase_));
  SocketAddress clientAddress;
  folly::TransportInfo tinfo;
  acceptor_->connectionReady(std::move(sock), clientAddress, proto, tinfo);
  EXPECT_EQ(acceptor_->sessionsCreated_, 1);
}

char const* protos1[] = { "spdy/3", "spdy/2", "http/1.1", "" };
INSTANTIATE_TEST_CASE_P(NPNPositive,
                        HTTPSessionAcceptorTestNPN,
                        ::testing::ValuesIn(protos1));

// Verify HTTPSessionAcceptor creates the correct plaintext codec
TEST_P(HTTPSessionAcceptorTestNPNPlaintext, plaintext_protocols) {
  std::string proto(GetParam());
  config_.plaintextProtocol = proto;
  newAcceptor();
  acceptor_->expectedProto_ = proto;
  AsyncSocket::UniquePtr sock(new AsyncSocket(&eventBase_));
  SocketAddress clientAddress;
  folly::TransportInfo tinfo;
  acceptor_->connectionReady(std::move(sock), clientAddress, "", tinfo);
  EXPECT_EQ(acceptor_->sessionsCreated_, 1);
}

char const* protos2[] = { "spdy/3", "spdy/2" };
INSTANTIATE_TEST_CASE_P(NPNPlaintext,
                        HTTPSessionAcceptorTestNPNPlaintext,
                        ::testing::ValuesIn(protos2));

// Verify HTTPSessionAcceptor closes the socket on invalid NPN
TEST_F(HTTPSessionAcceptorTestNPNJunk, npn) {
  std::string proto("/http/1.1");
  MockAsyncSocket::UniquePtr sock(new MockAsyncSocket(&eventBase_));
  SocketAddress clientAddress;
  folly::TransportInfo tinfo;
  EXPECT_CALL(*sock.get(), closeNow());
  acceptor_->connectionReady(std::move(sock), clientAddress, proto, tinfo);
  EXPECT_EQ(acceptor_->sessionsCreated_, 0);
}
