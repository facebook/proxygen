/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Foreach.h>
#include <folly/io/Cursor.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/TimeoutManager.h>
#include <gtest/gtest.h>
#include <proxygen/lib/http/codec/test/MockHTTPCodec.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <proxygen/lib/http/session/test/HTTPSessionMocks.h>
#include <proxygen/lib/http/session/test/HTTPSessionTest.h>
#include <proxygen/lib/http/session/test/TestUtils.h>
#include <proxygen/lib/test/TestAsyncTransport.h>
#include <string>
#include <folly/io/async/test/MockAsyncTransport.h>
#include <vector>

using folly::test::MockAsyncTransport;

using namespace folly;
using namespace proxygen;
using namespace testing;

using std::string;
using std::unique_ptr;
using std::vector;

struct HTTP1xCodecPair {
  typedef HTTP1xCodec Codec;
  static const int version = 1;
};

struct SPDY2CodecPair {
  typedef SPDYCodec Codec;
  static const SPDYVersion version = SPDYVersion::SPDY2;
};

struct SPDY3CodecPair {
  typedef SPDYCodec Codec;
  static const SPDYVersion version = SPDYVersion::SPDY3;
};

struct MockHTTPCodecPair {
  typedef MockHTTPCodec Codec;
  static const int version = 0;
};

template <class C>
class HTTPUpstreamTest: public testing::Test,
                        public HTTPSession::InfoCallback {
 public:
  HTTPUpstreamTest()
      : eventBase_(),
        transport_(new NiceMock<MockAsyncTransport>()),
        transactionTimeouts_(
          new AsyncTimeoutSet(&eventBase_,
                               TimeoutManager::InternalEnum::INTERNAL,
                               std::chrono::milliseconds(500))) {
  }

  virtual void onWriteChain(folly::AsyncTransportWrapper::WriteCallback* callback,
                            std::shared_ptr<IOBuf> iob,
                            WriteFlags flags) {
    if (saveWrites_) {
      auto mybuf = iob->clone();
      mybuf->unshare();
      writes_.append(std::move(mybuf));
    }
    if (failWrites_) {
      AsyncSocketException ex(AsyncSocketException::UNKNOWN, "");
      callback->writeErr(0, ex);
    } else {
      if (writeInLoop_) {
        eventBase_.runInLoop([&] {
            callback->writeSuccess();
          });
      } else {
        callback->writeSuccess();
      }
    }
  }

  void SetUp() override {
    commonSetUp(makeClientCodec<typename C::Codec>(C::version));
  }

  void commonSetUp(unique_ptr<HTTPCodec> codec) {
    HTTPSession::setPendingWriteMax(65536);
    EXPECT_CALL(*transport_, writeChain(_, _, _))
      .WillRepeatedly(Invoke(this, &HTTPUpstreamTest<C>::onWriteChain));
    EXPECT_CALL(*transport_, setReadCB(_))
      .WillRepeatedly(SaveArg<0>(&readCallback_));
    EXPECT_CALL(*transport_, getReadCB())
      .WillRepeatedly(Return(readCallback_));
    EXPECT_CALL(*transport_, getEventBase())
      .WillRepeatedly(Return(&eventBase_));
    EXPECT_CALL(*transport_, good())
      .WillRepeatedly(ReturnPointee(&transportGood_));
    EXPECT_CALL(*transport_, closeNow())
      .WillRepeatedly(Assign(&transportGood_, false));
    httpSession_ = new HTTPUpstreamSession(
      transactionTimeouts_.get(),
      std::move(AsyncTransportWrapper::UniquePtr(transport_)),
      localAddr_, peerAddr_,
      std::move(codec),
      mockTransportInfo_, this);
    httpSession_->startNow();
    eventBase_.loop();
    ASSERT_EQ(this->sessionDestroyed_, false);
  }

  unique_ptr<typename C::Codec> makeServerCodec() {
    return ::makeServerCodec<typename C::Codec>(C::version);
  }

  void readAndLoop(const char* input) {
    readAndLoop((const uint8_t *)input, strlen(input));
  }

  void readAndLoop(const uint8_t* input, size_t length) {
    CHECK_NOTNULL(readCallback_);
    void* buf;
    size_t bufSize;
    while (length > 0) {
      readCallback_->getReadBuffer(&buf, &bufSize);
      // This is somewhat specific to our implementation, but currently we
      // always return at least some space from getReadBuffer
      CHECK_GT(bufSize, 0);
      bufSize = std::min(bufSize, length);
      memcpy(buf, input, bufSize);
      readCallback_->readDataAvailable(bufSize);
      eventBase_.loop();
      length -= bufSize;
      input += bufSize;
    }
  }

  void testBasicRequest();
  void testBasicRequestHttp10(bool keepalive);

  // HTTPSession::InfoCallback interface
  void onCreate(const HTTPSession& sess) override { sessionCreated_ = true; }
  void onIngressError(const HTTPSession& sess, ProxygenError err) override {}
  void onRead(const HTTPSession& sess, size_t bytesRead) override {}
  void onWrite(const HTTPSession& sess, size_t bytesWritten) override {}
  void onRequestBegin(const HTTPSession& sess) override {}
  void onRequestEnd(const HTTPSession& sess,
                    uint32_t maxIngressQueueSize) override {}
  void onActivateConnection(const HTTPSession& sess) override {}
  void onDeactivateConnection(const HTTPSession& sess) override {}
  void onDestroy(const HTTPSession& sess) override { sessionDestroyed_ = true; }
  void onIngressMessage(const HTTPSession& sess,
                        const HTTPMessage& msg) override {}
  void onIngressLimitExceeded(const HTTPSession& sess) override {}
  void onIngressPaused(const HTTPSession& sess) override {}
  void onTransactionDetached(const HTTPSession& sess) override {}
  void onPingReplySent(int64_t latency) override {}
  void onPingReplyReceived() override {}
  void onSettingsOutgoingStreamsFull(const HTTPSession&) override {
    transactionsFull_ = true;
  }
  void onSettingsOutgoingStreamsNotFull(const HTTPSession&) override {
    transactionsFull_ = false;
  }
 protected:
  bool sessionCreated_{false};
  bool sessionDestroyed_{false};

  bool transactionsFull_{false};
  bool transportGood_{true};

  EventBase eventBase_;
  MockAsyncTransport* transport_;  // invalid once httpSession_ is destroyed
  folly::AsyncTransportWrapper::ReadCallback* readCallback_{nullptr};
  AsyncTimeoutSet::UniquePtr transactionTimeouts_;
  TransportInfo mockTransportInfo_;
  SocketAddress localAddr_{"127.0.0.1", 80};
  SocketAddress peerAddr_{"127.0.0.1", 12345};
  HTTPUpstreamSession* httpSession_{nullptr};
  IOBufQueue writes_{IOBufQueue::cacheChainLength()};
  bool saveWrites_{false};
  bool failWrites_{false};
  bool writeInLoop_{false};
};
TYPED_TEST_CASE_P(HTTPUpstreamTest);

template <class C>
class TimeoutableHTTPUpstreamTest: public HTTPUpstreamTest<C> {
 public:
  TimeoutableHTTPUpstreamTest(): HTTPUpstreamTest<C>() {
    // make it non-internal for this test class
    HTTPUpstreamTest<C>::transactionTimeouts_.reset(
      new AsyncTimeoutSet(&this->HTTPUpstreamTest<C>::eventBase_,
                           std::chrono::milliseconds(500)));
  }
};

typedef HTTPUpstreamTest<HTTP1xCodecPair> HTTPUpstreamSessionTest;
typedef HTTPUpstreamTest<SPDY2CodecPair> SPDY2UpstreamSessionTest;
typedef HTTPUpstreamTest<SPDY3CodecPair> SPDY3UpstreamSessionTest;

TEST_F(SPDY3UpstreamSessionTest, server_push) {
  SPDYCodec egressCodec(TransportDirection::DOWNSTREAM,
                        SPDYVersion::SPDY3);
  folly::IOBufQueue output(folly::IOBufQueue::cacheChainLength());

  HTTPMessage push;
  push.getHeaders().set("HOST", "www.foo.com");
  push.setURL("https://www.foo.com/");
  egressCodec.generateHeader(output, 2, push, 1, false, nullptr);
  auto buf = makeBuf(100);
  egressCodec.generateBody(output, 2, std::move(buf), HTTPCodec::NoPadding,
                           true /* eom */);

  HTTPMessage resp;
  resp.setStatusCode(200);
  resp.setStatusMessage("Ohai");
  egressCodec.generateHeader(output, 1, resp, 0, false, nullptr);
  buf = makeBuf(100);
  egressCodec.generateBody(output, 1, std::move(buf), HTTPCodec::NoPadding,
                           true /* eom */);

  std::unique_ptr<folly::IOBuf> input = output.move();
  input->coalesce();

  MockHTTPHandler handler;
  MockHTTPHandler pushHandler;
  HTTPTransaction* txn;

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&txn));
  EXPECT_CALL(handler, onPushedTransaction(_))
    .WillOnce(Invoke([this, &pushHandler] (HTTPTransaction* pushTxn) {
          pushTxn->setHandler(&pushHandler);
        }));
  EXPECT_CALL(pushHandler, setTransaction(_));
  EXPECT_CALL(pushHandler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_EQ(httpSession_->getNumIncomingStreams(), 1);
          EXPECT_FALSE(msg->getIsChunked());
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(msg->getPath(), "/");
          EXPECT_EQ(msg->getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST),
                    "www.foo.com");
        }));
  EXPECT_CALL(pushHandler, onBody(_));
  EXPECT_CALL(pushHandler, onEOM());
  EXPECT_CALL(pushHandler, detachTransaction());

  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  EXPECT_CALL(handler, onBody(_));
  EXPECT_CALL(handler, onEOM());
  EXPECT_CALL(handler, detachTransaction());

  HTTPTransaction* txn2 = httpSession_->newTransaction(&handler);
  CHECK_EQ(txn, txn2);

  HTTPMessage req = getGetRequest();

  txn->sendHeaders(req);
  txn->sendEOM();

  readAndLoop(input->data(), input->length());

  EXPECT_EQ(httpSession_->getNumIncomingStreams(), 0);
  httpSession_->destroy();
}

TEST_F(SPDY3UpstreamSessionTest, ingress_goaway_abort_uncreated_streams) {
  // Tests whether the session aborts the streams which are not created
  // at the remote end.
  MockHTTPHandler handler;
  HTTPTransaction* txn;

  // Create SPDY buf for GOAWAY with last good stream as 0 (no streams created)
  SPDYCodec egressCodec(TransportDirection::DOWNSTREAM,
                        SPDYVersion::SPDY3);
  folly::IOBufQueue respBuf{IOBufQueue::cacheChainLength()};
  egressCodec.generateGoaway(respBuf, 0, ErrorCode::NO_ERROR);
  std::unique_ptr<folly::IOBuf> goawayFrame = respBuf.move();
  goawayFrame->coalesce();

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&txn));
  EXPECT_CALL(handler, onError(_))
    .WillOnce(Invoke([&] (const HTTPException& err) {
          EXPECT_TRUE(err.hasProxygenError());
          EXPECT_EQ(err.getProxygenError(), kErrorStreamUnacknowledged);
          ASSERT_EQ(
            folly::to<std::string>("StreamUnacknowledged on transaction id: ",
              txn->getID()),
            std::string(err.what()));
        }));
  EXPECT_CALL(handler, detachTransaction())
    .WillOnce(InvokeWithoutArgs([this] {
          // Make sure the session can't create any more transactions.
          MockHTTPHandler handler2;
          EXPECT_EQ(httpSession_->newTransaction(&handler2), nullptr);
        }));

  // Create new transaction
  HTTPTransaction* txn2 = httpSession_->newTransaction(&handler);
  CHECK_EQ(txn, txn2);

  // Send the GET request
  HTTPMessage req = getGetRequest();
  txn->sendHeaders(req);
  txn->sendEOM();

  // Receive GOAWAY frame while waiting for SYN_REPLY
  readAndLoop(goawayFrame->data(), goawayFrame->length());

  // Session will delete itself after the abort
}

TEST_F(SPDY3UpstreamSessionTest, ingress_goaway_session_error) {
  // Tests whether the session aborts the streams which are not created
  // at the remote end which have error codes.
  MockHTTPHandler handler;
  HTTPTransaction* txn;

  // Create SPDY buf for GOAWAY with last good stream as 0 (no streams created)
  SPDYCodec egressCodec(TransportDirection::DOWNSTREAM,
                        SPDYVersion::SPDY3);
  folly::IOBufQueue respBuf{IOBufQueue::cacheChainLength()};
  egressCodec.generateGoaway(respBuf, 0, ErrorCode::PROTOCOL_ERROR);
  std::unique_ptr<folly::IOBuf> goawayFrame = respBuf.move();
  goawayFrame->coalesce();

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&txn));
  EXPECT_CALL(handler, onError(_))
    .WillOnce(Invoke([&] (const HTTPException& err) {
          EXPECT_TRUE(err.hasProxygenError());
          EXPECT_EQ(err.getProxygenError(), kErrorStreamUnacknowledged);
          ASSERT_EQ(
            folly::to<std::string>("StreamUnacknowledged on transaction id: ",
              txn->getID(),
              " with codec error: PROTOCOL_ERROR"),
            std::string(err.what()));
        }));
  EXPECT_CALL(handler, detachTransaction())
    .WillOnce(InvokeWithoutArgs([this] {
          // Make sure the session can't create any more transactions.
          MockHTTPHandler handler2;
          EXPECT_EQ(httpSession_->newTransaction(&handler2), nullptr);
        }));

  // Create new transaction
  HTTPTransaction* txn2 = httpSession_->newTransaction(&handler);
  CHECK_EQ(txn, txn2);

  // Send the GET request
  HTTPMessage req = getGetRequest();
  txn->sendHeaders(req);
  txn->sendEOM();

  // Receive GOAWAY frame while waiting for SYN_REPLY
  readAndLoop(goawayFrame->data(), goawayFrame->length());

  // Session will delete itself after the abort
}

TYPED_TEST_P(HTTPUpstreamTest, immediate_eof) {
  // Receive an EOF without any request data
  this->readCallback_->readEOF();
  this->eventBase_.loop();
  EXPECT_EQ(this->sessionDestroyed_, true);
}

template <class CodecPair>
void HTTPUpstreamTest<CodecPair>::testBasicRequest() {
  MockHTTPHandler handler;
  HTTPTransaction* txn;
  HTTPMessage req = getGetRequest();

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&txn));
  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_TRUE(msg->getIsChunked());
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  EXPECT_CALL(handler, onEOM());
  EXPECT_CALL(handler, detachTransaction());

  HTTPTransaction* txn2 = httpSession_->newTransaction(&handler);
  CHECK_EQ(txn, txn2);
  txn->sendHeaders(req);
  txn->sendEOM();
  readAndLoop("HTTP/1.1 200 OK\r\n"
              "Transfer-Encoding: chunked\r\n\r\n"
              "0\r\n\r\n");

  CHECK(httpSession_->supportsMoreTransactions());
  CHECK_EQ(httpSession_->getNumOutgoingStreams(), 0);
}

TEST_F(HTTPUpstreamSessionTest, basic_request) {
  testBasicRequest();
  httpSession_->destroy();
}

TEST_F(HTTPUpstreamSessionTest, two_requests) {
  testBasicRequest();
  testBasicRequest();
  httpSession_->destroy();
}

TEST_F(HTTPUpstreamSessionTest, 10_requests) {
  for (uint16_t i = 0; i < 10; i++) {
    testBasicRequest();
  }
  httpSession_->destroy();
}

template <class CodecPair>
void HTTPUpstreamTest<CodecPair>::testBasicRequestHttp10(bool keepalive) {
  MockHTTPHandler handler;
  HTTPTransaction* txn = nullptr;
  HTTPMessage req = getGetRequest();
  req.setHTTPVersion(1, 0);
  if (keepalive) {
    req.getHeaders().set(HTTP_HEADER_CONNECTION, "Keep-Alive");
  }

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&txn));
  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_EQ(200, msg->getStatusCode());
          EXPECT_EQ(keepalive ? "keep-alive" : "close",
            msg->getHeaders().getSingleOrEmpty(HTTP_HEADER_CONNECTION));
        }));
  EXPECT_CALL(handler, onBody(_));
  EXPECT_CALL(handler, onEOM());
  EXPECT_CALL(handler, detachTransaction());

  HTTPTransaction* txn2 = httpSession_->newTransaction(&handler);
  CHECK_EQ(txn, txn2);
  txn->sendHeaders(req);
  txn->sendEOM();
  if (keepalive) {
    readAndLoop("HTTP/1.0 200 OK\r\n"
                "Connection: keep-alive\r\n"
                "Content-length: 7\r\n\r\n"
                "content");
  } else {
    readAndLoop("HTTP/1.0 200 OK\r\n"
                "Connection: close\r\n"
                "Content-length: 7\r\n\r\n"
                "content");
  }
}

TEST_F(HTTPUpstreamSessionTest, http10_keepalive) {
  testBasicRequestHttp10(true);
  testBasicRequestHttp10(false);
}

TEST_F(HTTPUpstreamSessionTest, basic_trailers) {
  MockHTTPHandler handler;
  HTTPTransaction* txn;
  HTTPMessage req = getGetRequest();

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&txn));
  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_TRUE(msg->getIsChunked());
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  EXPECT_CALL(handler, onTrailers(_));
  EXPECT_CALL(handler, onEOM());
  EXPECT_CALL(handler, detachTransaction());

  HTTPTransaction* txn2 = httpSession_->newTransaction(&handler);
  CHECK_EQ(txn, txn2);
  txn->sendHeaders(req);
  txn->sendEOM();
  readAndLoop("HTTP/1.1 200 OK\r\n"
              "Transfer-Encoding: chunked\r\n\r\n"
              "0\r\n"
              "X-Trailer1: foo\r\n"
              "\r\n");

  CHECK(httpSession_->supportsMoreTransactions());
  CHECK_EQ(httpSession_->getNumOutgoingStreams(), 0);
  httpSession_->destroy();
}

TEST_F(HTTPUpstreamSessionTest, two_requests_with_pause) {
  MockHTTPHandler handler;
  HTTPTransaction* txn;
  HTTPMessage req = getGetRequest();

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&txn));
  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_TRUE(msg->getIsChunked());
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));

  HTTPTransaction* txn2 = httpSession_->newTransaction(&handler);
  CHECK_EQ(txn, txn2);

  EXPECT_CALL(handler, onEOM())
    .WillOnce(InvokeWithoutArgs([&] () {
          txn->pauseIngress();
        }));
  EXPECT_CALL(handler, detachTransaction());

  txn->sendHeaders(req);
  txn->sendEOM();
  readAndLoop("HTTP/1.1 200 OK\r\n"
              "Transfer-Encoding: chunked\r\n\r\n"
              "0\r\n\r\n");

  // Even though the previous transaction paused ingress just before it
  // finished up, reads resume automatically when the number of
  // transactions goes to zero. This way, the second request can read
  // without having to call resumeIngress()
  testBasicRequest();
  httpSession_->destroy();
}

typedef TimeoutableHTTPUpstreamTest<HTTP1xCodecPair> HTTPUpstreamTimeoutTest;
TEST_F(HTTPUpstreamTimeoutTest, write_timeout_after_response) {
  // Test where the upstream session times out while writing the request
  // to the server, but after the server has already sent back a full
  // response.
  MockHTTPHandler handler;
  HTTPMessage req = getPostRequest();

  InSequence dummy;
  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&(handler.txn_)));
  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_TRUE(msg->getIsChunked());
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  EXPECT_CALL(handler, onEOM());
  EXPECT_CALL(*transport_, writeChain(_, _, _))
    .WillRepeatedly(Return());  // ignore write -> write timeout
  EXPECT_CALL(handler, onError(_))
    .WillOnce(Invoke([&] (const HTTPException& err) {
          EXPECT_TRUE(err.hasProxygenError());
          ASSERT_EQ(err.getDirection(),
                    HTTPException::Direction::INGRESS_AND_EGRESS);
          EXPECT_EQ(err.getProxygenError(), kErrorWriteTimeout);
          ASSERT_EQ(
            folly::to<std::string>("WriteTimeout on transaction id: ",
              handler.txn_->getID()),
            std::string(err.what()));
        }));
  EXPECT_CALL(handler, detachTransaction());

  HTTPTransaction* txn = httpSession_->newTransaction(&handler);
  txn->sendHeaders(req);
  // Don't send the body, but get a response immediately
  readAndLoop("HTTP/1.1 200 OK\r\n"
              "Transfer-Encoding: chunked\r\n\r\n"
              "0\r\n\r\n");
}

TEST_F(HTTPUpstreamSessionTest, set_transaction_timeout) {
  // Test that setting a new timeout on the transaction will cancel
  // the old one.
  MockHTTPHandler handler;
  AsyncTimeoutSet::UniquePtr timeoutSet(
      new AsyncTimeoutSet(&eventBase_, std::chrono::milliseconds(500)));

  EXPECT_CALL(handler, setTransaction(_));
  EXPECT_CALL(handler, detachTransaction());

  auto txn = httpSession_->newTransaction(&handler);
  txn->setTransactionIdleTimeouts(timeoutSet.get());
  EXPECT_TRUE(txn->isScheduled());
  EXPECT_FALSE(transactionTimeouts_->front());
  EXPECT_TRUE(timeoutSet->front());
  txn->sendAbort();
  eventBase_.loop();
}

TEST_F(HTTPUpstreamSessionTest, 100_continue_keepalive) {
  // Test a request with 100 continue on a keepalive connection. Then make
  // another request.
  MockHTTPHandler handler;
  HTTPMessage req = getGetRequest();
  req.getHeaders().set(HTTP_HEADER_EXPECT, "100-continue");

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_));
  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_FALSE(msg->getIsChunked());
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(100, msg->getStatusCode());
        }))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_TRUE(msg->getIsChunked());
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  EXPECT_CALL(handler, onEOM());
  EXPECT_CALL(handler, detachTransaction());

  auto txn = httpSession_->newTransaction(&handler);
  txn->sendHeaders(req);
  txn->sendEOM();
  readAndLoop("HTTP/1.1 100 Continue\r\n\r\n"
              "HTTP/1.1 200 OK\r\n"
              "Transfer-Encoding: chunked\r\n\r\n"
              "0\r\n\r\n");

  // Now make sure everything still works
  testBasicRequest();
  httpSession_->destroy();
}

TEST_F(HTTPUpstreamSessionTest, 417_keepalive) {
  // Test a request with 100 continue on a keepalive connection. Then make
  // another request after the expectation fails.
  MockHTTPHandler handler;
  HTTPMessage req = getGetRequest();
  req.getHeaders().set(HTTP_HEADER_EXPECT, "100-continue");

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_));
  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_FALSE(msg->getIsChunked());
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(417, msg->getStatusCode());
        }));
  EXPECT_CALL(handler, onEOM());
  EXPECT_CALL(handler, detachTransaction());

  auto txn = httpSession_->newTransaction(&handler);
  txn->sendHeaders(req);
  txn->sendEOM();
  readAndLoop("HTTP/1.1 417 Expectation Failed\r\n"
              "Content-Length: 0\r\n\r\n");

  // Now make sure everything still works
  testBasicRequest();
  EXPECT_FALSE(sessionDestroyed_);
  httpSession_->destroy();
}

TEST_F(HTTPUpstreamSessionTest, 101_upgrade) {
  // Test an upgrade request with sending 101 response. Then send
  // some data and check the onBody callback contents
  MockHTTPHandler handler;
  HTTPMessage req = getGetRequest();
  req.getHeaders().set(HTTP_HEADER_UPGRADE, "http/2.0");

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_));
  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_FALSE(msg->getIsChunked());
          EXPECT_EQ(101, msg->getStatusCode());
        }));
  EXPECT_CALL(handler, onUpgrade(_));
  EXPECT_CALL(handler, onBody(_))
    .WillOnce(ExpectString("Test Body\r\n"));
  EXPECT_CALL(handler, onEOM());
  EXPECT_CALL(handler, detachTransaction());

  auto txn = httpSession_->newTransaction(&handler);
  txn->sendHeaders(req);
  txn->sendEOM();
  eventBase_.loop();
  readAndLoop("HTTP/1.1 101 Switching Protocols\r\n"
              "Upgrade: http/2.0\r\n\r\n"
              "Test Body\r\n");
  readCallback_->readEOF();
  eventBase_.loop();

  CHECK_EQ(httpSession_->getNumOutgoingStreams(), 0);
  httpSession_->destroy();
}

class NoFlushUpstreamSessionTest: public HTTPUpstreamTest<SPDY3CodecPair> {
 public:
  void onWriteChain(folly::AsyncTransportWrapper::WriteCallback* callback,
                    std::shared_ptr<IOBuf> iob,
                    WriteFlags flags) override {
    if (!timesCalled_++) {
      callback->writeSuccess();
    }
    // do nothing -- let unacked egress build up
  }
 private:
  uint32_t timesCalled_{0};
};

TEST_F(NoFlushUpstreamSessionTest, session_paused_start_paused) {
  // If the session is paused, new upstream transactions should start
  // paused too.
  NiceMock<MockHTTPHandler> handler1;
  NiceMock<MockHTTPHandler> handler2;
  HTTPMessage req = getGetRequest();

  InSequence dummy;

  EXPECT_CALL(handler1, setTransaction(_));
  auto txn1 = httpSession_->newTransaction(&handler1);
  txn1->sendHeaders(req);
  Mock::VerifyAndClearExpectations(&handler1);
  // The session pauses all txns since no writeSuccess for too many bytes
  EXPECT_CALL(handler1, onEgressPaused());
  // Send a body big enough to pause egress
  txn1->sendBody(makeBuf(HTTPSession::getPendingWriteMax()));
  eventBase_.loop();
  Mock::VerifyAndClearExpectations(&handler1);

  EXPECT_CALL(handler2, setTransaction(_));
  EXPECT_CALL(handler2, onEgressPaused());
  (void)httpSession_->newTransaction(&handler2);
  eventBase_.loop();
  Mock::VerifyAndClearExpectations(&handler2);

  httpSession_->shutdownTransportWithReset(kErrorTimeout);
}

TEST_F(NoFlushUpstreamSessionTest, delete_txn_on_unpause) {
  // Test where the handler gets onEgressResumed() and aborts itself and
  // creates another transaction on the SPDY session inside that
  // callback. This used to invalidate the transactions iterator inside
  // HTTPSession and cause a crash

  NiceMock<MockHTTPHandler> handler1;
  NiceMock<MockHTTPHandler> handler2;
  NiceMock<MockHTTPHandler> handler3;
  HTTPMessage req = getGetRequest();

  InSequence dummy;

  EXPECT_CALL(handler1, setTransaction(_));
  EXPECT_CALL(handler2, setTransaction(_));
  EXPECT_CALL(handler3, setTransaction(_));
  EXPECT_CALL(handler2, onEgressPaused())
    .WillOnce(InvokeWithoutArgs([this] {
          // This time it is invoked by the session on all transactions
          httpSession_->shutdownTransportWithReset(kErrorTimeout);
        }));
  /*auto txn1 =*/(void)httpSession_->newTransaction(&handler1);
  auto txn2 = httpSession_->newTransaction(&handler2);
  /*auto txn3 =*/(void)httpSession_->newTransaction(&handler3);
  txn2->sendHeaders(req);
  // This happens when the body write fills the txn egress queue
  // Send a body big enough to pause egress
  txn2->onIngressWindowUpdate(100);
  txn2->sendBody(makeBuf(HTTPSession::getPendingWriteMax() + 1));
  eventBase_.loop();
}

class MockHTTPUpstreamTest: public HTTPUpstreamTest<MockHTTPCodecPair> {
 public:
  void SetUp() override {
    auto codec = folly::make_unique<NiceMock<MockHTTPCodec>>();
    codecPtr_ = codec.get();
    EXPECT_CALL(*codec, supportsParallelRequests())
      .WillRepeatedly(Return(true));
    EXPECT_CALL(*codec, getTransportDirection())
      .WillRepeatedly(Return(TransportDirection::UPSTREAM));
    EXPECT_CALL(*codec, setCallback(_))
      .WillRepeatedly(SaveArg<0>(&codecCb_));
    EXPECT_CALL(*codec, isReusable())
      .WillRepeatedly(ReturnPointee(&reusable_));
    EXPECT_CALL(*codec, isWaitingToDrain())
      .WillRepeatedly(ReturnPointee(&reusable_));
    EXPECT_CALL(*codec, generateGoaway(_, _, _))
      .WillRepeatedly(Invoke([&] (IOBufQueue& writeBuf,
                                  HTTPCodec::StreamID lastStream,
                                  ErrorCode code){
            EXPECT_LT(lastStream, std::numeric_limits<int32_t>::max());
            if (reusable_) {
              writeBuf.append("GOAWAY", 6);
              reusable_ = false;
            }
            return 6;
          }));
    EXPECT_CALL(*codec, createStream())
      .WillRepeatedly(Invoke([&] {
            auto ret = nextOutgoingTxn_;
            nextOutgoingTxn_ += 2;
            return ret;
          }));

    commonSetUp(std::move(codec));
  }

  void TearDown() override {
    EXPECT_TRUE(sessionDestroyed_);
  }

  std::unique_ptr<StrictMock<MockHTTPHandler>> openTransaction() {
    // Returns a mock handler with txn_ field set in it
    auto handler = folly::make_unique<StrictMock<MockHTTPHandler>>();
    EXPECT_CALL(*handler, setTransaction(_))
      .WillOnce(SaveArg<0>(&handler->txn_));
    auto txn = httpSession_->newTransaction(handler.get());
    EXPECT_EQ(txn, handler->txn_);
    return std::move(handler);
  }

  MockHTTPCodec* codecPtr_{nullptr};
  HTTPCodec::Callback* codecCb_{nullptr};
  bool reusable_{true};
  uint32_t nextOutgoingTxn_{1};
};

class MockHTTP2UpstreamTest: public MockHTTPUpstreamTest {
 public:
  void SetUp() override {
    MockHTTPUpstreamTest::SetUp();

    // This class assumes we are doing a test for SPDY or HTTP/2+ where
    // this function is *not* a no-op. Indicate this via a positive number
    // of bytes being generated for writing RST_STREAM.

    ON_CALL(*codecPtr_, generateRstStream(_, _, _))
      .WillByDefault(Return(1));
  }
};


TEST_F(MockHTTP2UpstreamTest, parse_error_no_txn) {
  // 1) Create streamID == 1
  // 2) Send request
  // 3) Detach handler
  // 4) Get an ingress parse error on reply
  // Expect that the codec should be asked to generate an abort on streamID==1

  // Setup the codec expectations.
  EXPECT_CALL(*codecPtr_, generateEOM(_, _))
    .WillOnce(Return(20));
  EXPECT_CALL(*codecPtr_, generateRstStream(_, 1, _));

  // 1)
  NiceMock<MockHTTPHandler> handler;
  auto txn = httpSession_->newTransaction(&handler);

  // 2)
  auto req = getPostRequest();
  txn->sendHeaders(req);
  txn->sendEOM();

  // 3) Note this sendAbort() doesn't destroy the txn since byte events are
  // enqueued
  txn->sendAbort();

  // 4)
  HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS, "foo");
  ex.setProxygenError(kErrorParseHeader);
  ex.setCodecStatusCode(ErrorCode::REFUSED_STREAM);
  codecCb_->onError(1, ex, true);

  // cleanup
  httpSession_->shutdownTransportWithReset(kErrorConnectionReset);
  eventBase_.loop();
}

TEST_F(MockHTTPUpstreamTest, 0_max_outgoing_txns) {
  // Test where an upstream session gets a SETTINGS frame with 0 max
  // outgoing transactions. In our implementation, we jsut send a GOAWAY
  // and close the connection.

  codecCb_->onSettings({{SettingsId::MAX_CONCURRENT_STREAMS, 0}});
  EXPECT_TRUE(transactionsFull_);
  httpSession_->shutdownTransportWithReset(kErrorConnectionReset);
}

TEST_F(MockHTTPUpstreamTest, outgoing_txn_settings) {
  // Create 2 transactions, then receive a settings frame from
  // the server indicating 1 parallel transaction at a time is allowed.
  // Then get another SETTINGS frame indicating 100 max parallel
  // transactions. Expect that HTTPSession invokes both info callbacks.

  NiceMock<MockHTTPHandler> handler1;
  NiceMock<MockHTTPHandler> handler2;
  httpSession_->newTransaction(&handler1);
  httpSession_->newTransaction(&handler2);

  codecCb_->onSettings({{SettingsId::MAX_CONCURRENT_STREAMS, 1}});
  EXPECT_TRUE(transactionsFull_);
  codecCb_->onSettings({{SettingsId::MAX_CONCURRENT_STREAMS, 100}});
  EXPECT_FALSE(transactionsFull_);
  httpSession_->shutdownTransportWithReset(kErrorConnectionReset);
}

TEST_F(MockHTTPUpstreamTest, ingress_goaway_drain) {
  // Tests whether the session drains existing transactions and
  // deletes itself after receiving a GOAWAY.
  MockHTTPHandler handler;
  HTTPTransaction* txn;

  InSequence dummy;

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&txn));
  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  EXPECT_CALL(handler, onEOM());
  EXPECT_CALL(handler, detachTransaction());

  // Create new transaction
  HTTPTransaction* txn2 = httpSession_->newTransaction(&handler);
  CHECK_EQ(txn, txn2);

  // Send the GET request
  HTTPMessage req = getGetRequest();
  txn->sendHeaders(req);
  txn->sendEOM();

  // Receive GOAWAY frame with last good stream as 1
  codecCb_->onGoaway(1, ErrorCode::NO_ERROR);

  // New transactions cannot be created afrer goaway
  EXPECT_FALSE(httpSession_->isReusable());
  EXPECT_EQ(httpSession_->newTransaction(&handler), nullptr);

  // Receive 200 OK
  auto resp = makeResponse(200);
  codecCb_->onMessageBegin(1, resp.get());
  codecCb_->onHeadersComplete(1, std::move(resp));
  codecCb_->onMessageComplete(1, false);
  eventBase_.loop();

  // Session will delete itself after getting the response
}

TEST_F(MockHTTPUpstreamTest, goaway) {
  // Make sure existing txns complete successfully even if we drain the
  // upstream session
  const unsigned numTxns = 10;
  MockHTTPHandler handler[numTxns];

  InSequence dummy;

  for (unsigned i = 0; i < numTxns; ++i) {
    EXPECT_CALL(handler[i], setTransaction(_))
      .WillOnce(SaveArg<0>(&handler[i].txn_));
    EXPECT_CALL(handler[i], onHeadersComplete(_))
      .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
            EXPECT_FALSE(msg->getIsUpgraded());
            EXPECT_EQ(200, msg->getStatusCode());
          }));
    httpSession_->newTransaction(&handler[i]);

    // Send the GET request
    HTTPMessage req = getGetRequest();
    handler[i].txn_->sendHeaders(req);
    handler[i].txn_->sendEOM();

    // Receive 200 OK
    auto resp = makeResponse(200);
    codecCb_->onMessageBegin(handler[i].txn_->getID(), resp.get());
    codecCb_->onHeadersComplete(handler[i].txn_->getID(), std::move(resp));
  }

  codecCb_->onGoaway(numTxns * 2 + 1, ErrorCode::NO_ERROR);
  for (unsigned i = 0; i < numTxns; ++i) {
    EXPECT_CALL(handler[i], onEOM());
    EXPECT_CALL(handler[i], detachTransaction());
    codecCb_->onMessageComplete(i*2 + 1, false);
  }
  eventBase_.loop();

  // Session will delete itself after drain completes
}

TEST_F(MockHTTPUpstreamTest, goaway_pre_headers) {
  // Make sure existing txns complete successfully even if we drain the
  // upstream session
  MockHTTPHandler handler;

  InSequence dummy;
  saveWrites_ = true;

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&handler.txn_));
  EXPECT_CALL(*codecPtr_, generateHeader(_, _, _, _, _, _))
    .WillOnce(Invoke([&] (IOBufQueue& writeBuf,
                          HTTPCodec::StreamID stream,
                          const HTTPMessage& msg,
                          HTTPCodec::StreamID assocStream,
                          bool eom,
                          HTTPHeaderSize* size) {
                       writeBuf.append("HEADERS", 7);
                     }));
  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  httpSession_->newTransaction(&handler);
  httpSession_->drain();

  // Send the GET request
  HTTPMessage req = getGetRequest();
  handler.txn_->sendHeaders(req);
  handler.txn_->sendEOM();

  // Receive 200 OK
  auto resp = makeResponse(200);
  codecCb_->onMessageBegin(handler.txn_->getID(), resp.get());
  codecCb_->onHeadersComplete(handler.txn_->getID(), std::move(resp));

  codecCb_->onGoaway(1, ErrorCode::NO_ERROR);
  EXPECT_CALL(handler, onEOM());
  EXPECT_CALL(handler, detachTransaction());
  codecCb_->onMessageComplete(1, false);
  eventBase_.loop();

  auto buf = writes_.move();
  ASSERT_TRUE(buf != nullptr);
  EXPECT_EQ(buf->moveToFbString().data(), string("HEADERSGOAWAY"));
  // Session will delete itself after drain completes
}

TEST_F(MockHTTPUpstreamTest, no_window_update_on_drain) {
  MockHTTPHandler handler;
  HTTPTransaction* txn;

  EXPECT_CALL(*codecPtr_, supportsStreamFlowControl())
    .WillRepeatedly(Return(true));

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&txn));

  HTTPTransaction* txn2 = httpSession_->newTransaction(&handler);

  HTTPMessage req = getGetRequest();

  txn->sendHeaders(req);
  txn->sendEOM();
  httpSession_->drain();
  auto streamID = txn->getID();

  EXPECT_CALL(handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  EXPECT_CALL(handler, onBody(_))
    .Times(3);
  EXPECT_CALL(handler, onEOM());

  EXPECT_CALL(handler, detachTransaction());

  CHECK_EQ(txn, txn2);

  uint32_t outstanding = 0;
  uint32_t sendWindow = 65536;
  uint32_t toSend = sendWindow * 1.55;

  // We'll get exactly one window update because we are draining
  EXPECT_CALL(*codecPtr_, generateWindowUpdate(_, _, _))
    .WillOnce(Invoke([&]
                     (folly::IOBufQueue& writeBuf,
                      HTTPCodec::StreamID stream,
                      uint32_t delta) {
                       EXPECT_EQ(delta, sendWindow);
                       outstanding -= delta;
                       uint32_t len = std::min(toSend,
                                               sendWindow - outstanding);
                       EXPECT_LT(len, sendWindow);
                       toSend -= len;
                       EXPECT_EQ(toSend, 0);
                       eventBase_.tryRunAfterDelay([this, streamID, len] {
                           failWrites_ = true;
                           auto respBody = makeBuf(len);
                           codecCb_->onBody(streamID, std::move(respBody), 0);
                           codecCb_->onMessageComplete(streamID, false);
                         }, 50);

                       const std::string dummy("window");
                       writeBuf.append(dummy);
                       return 6;
                     }));

  codecCb_->onGoaway(streamID, ErrorCode::NO_ERROR);
  auto resp = makeResponse(200);
  codecCb_->onMessageBegin(streamID, resp.get());
  codecCb_->onHeadersComplete(streamID, std::move(resp));

  // While there is room and the window and body to send
  while (sendWindow - outstanding > 0 && toSend > 0) {
    // Send up to a 36k chunk
    uint32_t len = std::min(toSend, uint32_t(36000));
    // limited by the available window
    len = std::min(len, sendWindow - outstanding);
    auto respBody = makeBuf(len);
    toSend -= len;
    outstanding += len;
    codecCb_->onBody(streamID, std::move(respBody), 0);
  }

  eventBase_.loop();
}

TEST_F(MockHTTPUpstreamTest, get_with_body) {
  // Should be allowed to send a GET request with body.
  NiceMock<MockHTTPHandler> handler;
  HTTPMessage req = getGetRequest();
  req.getHeaders().set(HTTP_HEADER_CONTENT_LENGTH, "10");

  InSequence dummy;

  EXPECT_CALL(*codecPtr_, generateHeader(_, _, _, _, _, _));
  EXPECT_CALL(*codecPtr_, generateBody(_, _, _, _, true));

  auto txn = httpSession_->newTransaction(&handler);
  txn->sendHeaders(req);
  txn->sendBody(makeBuf(10));
  txn->sendEOM();

  eventBase_.loop();
  httpSession_->shutdownTransportWithReset(kErrorConnectionReset);
}

template <int stage>
class TestAbortPost : public MockHTTPUpstreamTest {
 public:
  void doAbortTest() {
    // Send an abort at various points while receiving the response to a GET
    // The test is broken into "stages"
    // Stage 0) headers received
    // Stage 1) chunk header received
    // Stage 2) body received
    // Stage 3) chunk complete received
    // Stage 4) trailers received
    // Stage 5) eom received
    // This test makes sure expected callbacks are received if an abort is
    // sent before any of these stages.
    InSequence enforceOrder;
    StrictMock<MockHTTPHandler> handler;
    HTTPMessage req = getPostRequest();
    req.getHeaders().set(HTTP_HEADER_CONTENT_LENGTH, "10");

    std::unique_ptr<HTTPMessage> resp;
    std::unique_ptr<folly::IOBuf> respBody;
    std::tie(resp, respBody) = makeResponse(200, 50);

    EXPECT_CALL(handler, setTransaction(_))
      .WillOnce(SaveArg<0>(&handler.txn_));
    EXPECT_CALL(*codecPtr_, generateHeader(_, _, _, _, _, _));

    if (stage > 0) {
      EXPECT_CALL(handler, onHeadersComplete(_));
    }
    if (stage > 1) {
      EXPECT_CALL(handler, onChunkHeader(_));
    }
    if (stage > 2) {
      EXPECT_CALL(handler, onBody(_));
    }
    if (stage > 3) {
      EXPECT_CALL(handler, onChunkComplete());
    }
    if (stage > 4) {
      EXPECT_CALL(handler, onTrailers(_));
    }
    if (stage > 5) {
      EXPECT_CALL(handler, onEOM());
    }

    auto txn = httpSession_->newTransaction(&handler);
    auto streamID = txn->getID();
    txn->sendHeaders(req);
    txn->sendBody(makeBuf(5)); // only send half the body

    auto doAbort = [&] {
      EXPECT_CALL(*codecPtr_, generateRstStream(_, txn->getID(), _));
      EXPECT_CALL(handler, detachTransaction());
      const auto id = txn->getID();
      txn->sendAbort();
      EXPECT_CALL(*codecPtr_,
                  generateRstStream(_, id, ErrorCode::_SPDY_INVALID_STREAM))
        .Times(AtLeast(0));
    };

    if (stage == 0) {
      doAbort();
    }
    codecCb_->onHeadersComplete(streamID, std::move(resp));
    if (stage == 1) {
      doAbort();
    }
    codecCb_->onChunkHeader(streamID, respBody->computeChainDataLength());
    if (stage == 2) {
      doAbort();
    }
    codecCb_->onBody(streamID, std::move(respBody), 0);
    if (stage == 3) {
      doAbort();
    }
    codecCb_->onChunkComplete(streamID);
    if (stage == 4) {
      doAbort();
    }
    codecCb_->onTrailersComplete(streamID,
                                 folly::make_unique<HTTPHeaders>());
    if (stage == 5) {
      doAbort();
    }
    codecCb_->onMessageComplete(streamID, false);

    eventBase_.loop();
  }
};

typedef TestAbortPost<0> TestAbortPost0;
typedef TestAbortPost<1> TestAbortPost1;
typedef TestAbortPost<2> TestAbortPost2;
typedef TestAbortPost<3> TestAbortPost3;
typedef TestAbortPost<4> TestAbortPost4;
typedef TestAbortPost<5> TestAbortPost5;

TEST_F(TestAbortPost1, test) { doAbortTest(); }
TEST_F(TestAbortPost2, test) { doAbortTest(); }
TEST_F(TestAbortPost3, test) { doAbortTest(); }
TEST_F(TestAbortPost4, test) { doAbortTest(); }
TEST_F(TestAbortPost5, test) { doAbortTest(); }

TEST_F(MockHTTPUpstreamTest, abort_upgrade) {
  // This is basically the same test as above, just for the upgrade path
  InSequence enforceOrder;
  StrictMock<MockHTTPHandler> handler;
  HTTPMessage req = getPostRequest();
  req.getHeaders().set(HTTP_HEADER_CONTENT_LENGTH, "10");

  std::unique_ptr<HTTPMessage> resp = makeResponse(200);

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&handler.txn_));
  EXPECT_CALL(*codecPtr_, generateHeader(_, _, _, _, _, _));

  auto txn = httpSession_->newTransaction(&handler);
  const auto streamID = txn->getID();
  txn->sendHeaders(req);
  txn->sendBody(makeBuf(5)); // only send half the body

  EXPECT_CALL(handler, onHeadersComplete(_));
  codecCb_->onHeadersComplete(streamID, std::move(resp));

  EXPECT_CALL(*codecPtr_, generateRstStream(_, streamID, _));
  EXPECT_CALL(handler, detachTransaction());
  EXPECT_CALL(*codecPtr_, generateRstStream(_, streamID,
                                            ErrorCode::_SPDY_INVALID_STREAM));
  txn->sendAbort();
  codecCb_->onMessageComplete(streamID, true); // upgrade
  EXPECT_CALL(*codecPtr_, generateRstStream(_, streamID,
                                            ErrorCode::_SPDY_INVALID_STREAM));
  codecCb_->onMessageComplete(streamID, false); // eom

  eventBase_.loop();
}

TEST_F(MockHTTPUpstreamTest, drain_before_send_headers) {
  // Test that drain on session before sendHeaders() is called on open txn

  InSequence enforceOrder;
  NiceMock<MockHTTPHandler> handler;
  MockHTTPHandler pushHandler;
  auto req = makeGetRequest();

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(SaveArg<0>(&handler.txn_));
  EXPECT_CALL(*codecPtr_, generateHeader(_, _, _, _, _, _));

  EXPECT_CALL(handler, onHeadersComplete(_));
  EXPECT_CALL(handler, detachTransaction());

  auto txn = httpSession_->newTransaction(&handler);
  httpSession_->drain();
  txn->sendHeaders(*req);
  txn->sendEOM();
  codecCb_->onHeadersComplete(txn->getID(), makeResponse(200));
  codecCb_->onMessageComplete(txn->getID(), false); // eom

  eventBase_.loop();
}

TEST_F(MockHTTP2UpstreamTest, receive_double_goaway) {
  // Test that we handle receiving two goaways correctly

  InSequence enforceOrder;
  auto req = getGetRequest();

  // Open 2 txns but doesn't send headers yet
  auto handler1 = openTransaction();
  auto handler2 = openTransaction();

  // Get first goaway acking many un-started txns
  codecCb_->onGoaway(101, ErrorCode::NO_ERROR);

  // This txn should be alive since it was ack'd by the above goaway
  handler1->txn_->sendHeaders(req);

  // Second goaway acks the only the current outstanding transaction
  EXPECT_CALL(*handler2, onError(_))
    .WillOnce(Invoke([&] (const HTTPException& err) {
          EXPECT_TRUE(err.hasProxygenError());
          EXPECT_EQ(err.getProxygenError(), kErrorStreamUnacknowledged);
          ASSERT_EQ(
            folly::to<std::string>("StreamUnacknowledged on transaction id: ",
              handler2->txn_->getID()),
            std::string(err.what()));
        }));
  EXPECT_CALL(*handler2, detachTransaction());
  codecCb_->onGoaway(handler1->txn_->getID(), ErrorCode::NO_ERROR);

  // Clean up
  httpSession_->drain();
  EXPECT_CALL(*codecPtr_, generateRstStream(_, handler1->txn_->getID(), _));
  EXPECT_CALL(*handler1, detachTransaction());
  handler1->txn_->sendAbort();
  eventBase_.loop();
}

TEST_F(MockHTTP2UpstreamTest, server_push_invalid_assoc) {
  // Test that protocol error is generated on server push
  // with invalid assoc stream id
  InSequence enforceOrder;
  auto req = getGetRequest();
  auto handler = openTransaction();

  int streamID = handler->txn_->getID();
  int pushID = streamID + 1;
  int badAssocID = streamID + 2;

  EXPECT_CALL(*codecPtr_,
              generateRstStream(_, pushID, ErrorCode::PROTOCOL_ERROR));
  EXPECT_CALL(*codecPtr_,
              generateRstStream(_, pushID, ErrorCode::_SPDY_INVALID_STREAM))
    .Times(2);

  auto resp = makeResponse(200);
  codecCb_->onPushMessageBegin(pushID, badAssocID, resp.get());
  codecCb_->onHeadersComplete(pushID, std::move(resp));
  codecCb_->onMessageComplete(pushID, false);

  EXPECT_CALL(*handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  EXPECT_CALL(*handler, onEOM());

  resp = makeResponse(200);
  codecCb_->onMessageBegin(streamID, resp.get());
  codecCb_->onHeadersComplete(streamID, std::move(resp));
  codecCb_->onMessageComplete(streamID, false);

  // Cleanup
  EXPECT_CALL(*codecPtr_, generateRstStream(_, streamID, _));
  EXPECT_CALL(*handler, detachTransaction());
  handler->terminate();

  EXPECT_TRUE(!httpSession_->hasActiveTransactions());
  httpSession_->destroy();
}

TEST_F(MockHTTP2UpstreamTest, server_push_after_fin) {
  // Test that protocol error is generated on server push
  // after FIN is received on regular response on the stream
  InSequence enforceOrder;
  auto req = getGetRequest();
  auto handler = openTransaction();

  int streamID = handler->txn_->getID();
  int pushID = streamID + 1;

  EXPECT_CALL(*handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  EXPECT_CALL(*handler, onEOM());

  auto resp = makeResponse(200);
  codecCb_->onMessageBegin(streamID, resp.get());
  codecCb_->onHeadersComplete(streamID, std::move(resp));
  codecCb_->onMessageComplete(streamID, false);

  EXPECT_CALL(*codecPtr_,
              generateRstStream(_, pushID, ErrorCode::PROTOCOL_ERROR))
    .WillOnce(InvokeWithoutArgs([this] {
            // Verify that the assoc txn is still present
            EXPECT_TRUE(httpSession_->hasActiveTransactions());
            return 1;
          }));
  EXPECT_CALL(*codecPtr_,
              generateRstStream(_, pushID, ErrorCode::_SPDY_INVALID_STREAM))
    .Times(2);

  resp = makeResponse(200);
  codecCb_->onPushMessageBegin(pushID, streamID, resp.get());
  codecCb_->onHeadersComplete(pushID, std::move(resp));
  codecCb_->onMessageComplete(pushID, false);

  // Cleanup
  EXPECT_CALL(*codecPtr_, generateRstStream(_, streamID, _));
  EXPECT_CALL(*handler, detachTransaction());
  handler->terminate();

  EXPECT_TRUE(!httpSession_->hasActiveTransactions());
  httpSession_->destroy();
}

TEST_F(MockHTTP2UpstreamTest, server_push_handler_install_fail) {
  // Test that REFUSED_STREAM error is generated when the session
  // fails to install the server push handler
  InSequence enforceOrder;
  auto req = getGetRequest();
  auto handler = openTransaction();

  int streamID = handler->txn_->getID();
  int pushID = streamID + 1;

  EXPECT_CALL(*handler, onPushedTransaction(_))
    .WillOnce(Invoke([] (HTTPTransaction* txn) {
            // Intentionally unset the handler on the upstream push txn
            txn->setHandler(nullptr);
          }));
  EXPECT_CALL(*codecPtr_,
              generateRstStream(_, pushID, ErrorCode::REFUSED_STREAM));
  EXPECT_CALL(*codecPtr_,
              generateRstStream(_, pushID, ErrorCode::_SPDY_INVALID_STREAM))
    .Times(2);

  auto resp = folly::make_unique<HTTPMessage>();
  resp->setStatusCode(200);
  resp->setStatusMessage("OK");
  codecCb_->onPushMessageBegin(pushID, streamID, resp.get());
  codecCb_->onHeadersComplete(pushID, std::move(resp));
  codecCb_->onMessageComplete(pushID, false);

  EXPECT_CALL(*handler, onHeadersComplete(_))
    .WillOnce(Invoke([&] (std::shared_ptr<HTTPMessage> msg) {
          EXPECT_FALSE(msg->getIsUpgraded());
          EXPECT_EQ(200, msg->getStatusCode());
        }));
  EXPECT_CALL(*handler, onEOM());

  resp = folly::make_unique<HTTPMessage>();
  resp->setStatusCode(200);
  resp->setStatusMessage("OK");
  codecCb_->onMessageBegin(streamID, resp.get());
  codecCb_->onHeadersComplete(streamID, std::move(resp));
  codecCb_->onMessageComplete(streamID, false);

  // Cleanup
  EXPECT_CALL(*codecPtr_, generateRstStream(_, streamID, _));
  EXPECT_CALL(*handler, detachTransaction());
  handler->terminate();

  EXPECT_TRUE(!httpSession_->hasActiveTransactions());
  httpSession_->destroy();
}

TEST_F(MockHTTP2UpstreamTest, server_push_unhandled_assoc) {
  // Test that REFUSED_STREAM error is generated when the assoc txn
  // is unhandled
  InSequence enforceOrder;
  auto req = getGetRequest();
  auto handler = openTransaction();

  int streamID = handler->txn_->getID();
  int pushID = streamID + 1;

  // Forcefully unset the handler on the assoc txn
  handler->txn_->setHandler(nullptr);

  EXPECT_CALL(*codecPtr_,
              generateRstStream(_, pushID, ErrorCode::REFUSED_STREAM));
  EXPECT_CALL(*codecPtr_,
              generateRstStream(_, pushID, ErrorCode::_SPDY_INVALID_STREAM))
    .Times(2);

  auto resp = folly::make_unique<HTTPMessage>();
  resp->setStatusCode(200);
  resp->setStatusMessage("OK");
  codecCb_->onPushMessageBegin(pushID, streamID, resp.get());
  codecCb_->onHeadersComplete(pushID, std::move(resp));
  codecCb_->onMessageComplete(pushID, false);

  // Cleanup
  EXPECT_CALL(*codecPtr_, generateRstStream(_, streamID, _));
  handler->terminate();

  EXPECT_TRUE(!httpSession_->hasActiveTransactions());
  httpSession_->destroy();
}

TEST_F(MockHTTPUpstreamTest, headers_then_body_then_headers) {
  HTTPMessage req = getGetRequest();
  auto handler = openTransaction();
  handler->txn_->sendHeaders(req);

  EXPECT_CALL(*handler, onHeadersComplete(_));
  EXPECT_CALL(*handler, onBody(_));
  // After getting the second headers, transaction will detach the handler
  EXPECT_CALL(*handler, onError(_))
    .WillOnce(Invoke([&] (const HTTPException& err) {
          EXPECT_TRUE(err.hasProxygenError());
          EXPECT_EQ(err.getProxygenError(), kErrorIngressStateTransition);
          ASSERT_EQ(
            "Invalid ingress state transition, state=RegularBodyReceived, "
            "event=onHeaders, streamID=1",
            std::string(err.what()));
        }));
  EXPECT_CALL(*handler, detachTransaction());
  auto resp = makeResponse(200);
  codecCb_->onMessageBegin(1, resp.get());
  codecCb_->onHeadersComplete(1, std::move(resp));
  codecCb_->onBody(1, makeBuf(20), 0);
  // Now receive headers again, on the same stream (illegal!)
  codecCb_->onHeadersComplete(1, makeResponse(200));
  eventBase_.loop();
}

TEST_F(MockHTTP2UpstreamTest, delay_upstream_window_update) {
  auto handler = openTransaction();
  handler->txn_->setReceiveWindow(1000000); // One miiiillion

  InSequence dummy;
  EXPECT_CALL(*codecPtr_, generateHeader(_, _, _, _, _, _));
  EXPECT_CALL(*codecPtr_, generateWindowUpdate(_, _, _));

  HTTPMessage req = getGetRequest();
  handler->txn_->sendHeaders(req);
  EXPECT_CALL(*handler, detachTransaction());
  handler->txn_->sendAbort();
  httpSession_->destroy();
}

TEST_F(MockHTTPUpstreamTest, force_shutdown_in_set_transaction) {
  StrictMock<MockHTTPHandler> handler;
  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(DoAll(
      SaveArg<0>(&(handler.txn_)),
      Invoke([&] (HTTPTransaction* txn) {
          httpSession_->shutdownTransportWithReset(kErrorNone);
        })));
  EXPECT_CALL(handler, onError(_))
    .WillOnce(Invoke([&] (const HTTPException& err) {
          EXPECT_FALSE(err.hasProxygenError());
          ASSERT_EQ(
            folly::to<std::string>("None on transaction id: ",
              handler.txn_->getID()),
            std::string(err.what()));
        }));
  EXPECT_CALL(handler, detachTransaction());
  (void)httpSession_->newTransaction(&handler);
}

// Register and instantiate all our type-paramterized tests
REGISTER_TYPED_TEST_CASE_P(HTTPUpstreamTest,
                           immediate_eof/*[, other_test]*/);

typedef ::testing::Types<HTTP1xCodecPair, SPDY2CodecPair,
                         SPDY3CodecPair> AllTypes;
INSTANTIATE_TYPED_TEST_CASE_P(AllTypesPrefix, HTTPUpstreamTest, AllTypes);
