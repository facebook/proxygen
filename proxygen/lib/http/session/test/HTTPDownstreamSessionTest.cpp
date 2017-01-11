/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Conv.h>
#include <folly/futures/Promise.h>
#include <wangle/acceptor/ConnectionManager.h>
#include <folly/io/Cursor.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/TimeoutManager.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <proxygen/lib/http/codec/HTTPCodecFactory.h>
#include <proxygen/lib/http/session/HTTPDirectResponseHandler.h>
#include <proxygen/lib/http/session/HTTPDownstreamSession.h>
#include <proxygen/lib/http/session/HTTPSession.h>
#include <proxygen/lib/http/session/test/HTTPSessionMocks.h>
#include <proxygen/lib/http/session/test/HTTPSessionTest.h>
#include <proxygen/lib/http/session/test/MockByteEventTracker.h>
#include <proxygen/lib/http/session/test/TestUtils.h>
#include <proxygen/lib/test/TestAsyncTransport.h>
#include <string>
#include <folly/io/async/test/MockAsyncTransport.h>
#include <vector>

using namespace folly::io;
using namespace wangle;
using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;
using namespace std::chrono;
using folly::Promise;

template <typename C>
class HTTPDownstreamTest : public testing::Test {
 public:
  explicit HTTPDownstreamTest(
    std::vector<int64_t> flowControl = { -1, -1, -1 })
    : eventBase_(),
      transport_(new TestAsyncTransport(&eventBase_)),
      transactionTimeouts_(makeTimeoutSet(&eventBase_)),
      flowControl_(flowControl) {
    EXPECT_CALL(mockController_, getGracefulShutdownTimeout())
      .WillRepeatedly(Return(std::chrono::milliseconds(0)));
    EXPECT_CALL(mockController_, attachSession(_));
    HTTPSession::setDefaultReadBufferLimit(65536);
    httpSession_ = new HTTPDownstreamSession(
      transactionTimeouts_.get(),
      std::move(AsyncTransportWrapper::UniquePtr(transport_)),
      localAddr, peerAddr,
      &mockController_,
      std::move(makeServerCodec<typename C::Codec>(
                  C::version)),
      mockTransportInfo /* no stats for now */);
    for (auto& param: flowControl) {
      if (param < 0) {
        param = httpSession_->getCodec().getDefaultWindowSize();
      }
    }
    httpSession_->setFlowControl(flowControl[0], flowControl[1],
                                 flowControl[2]);
    httpSession_->startNow();
    clientCodec_ = makeClientCodec<typename C::Codec>(C::version);
    clientCodec_->generateConnectionPreface(requests_);
    clientCodec_->setCallback(&callbacks_);
  }

  typename C::Codec& getCodec() {
    return
      (typename C::Codec&)httpSession_->getCodecFilterChain().getChainEnd();
  }

  HTTPCodec::StreamID sendRequest(const std::string& url = "/",
                                  int8_t priority = 0,
                                  bool eom = true) {
    auto req = getGetRequest();
    req.setURL(url);
    req.setPriority(priority);
    return sendRequest(req, eom);
  }

  HTTPCodec::StreamID sendRequest(const HTTPMessage& req, bool eom = true) {
    auto streamID = clientCodec_->createStream();
    clientCodec_->generateHeader(requests_, streamID, req, HTTPCodec::NoStream,
                                 eom);
    return streamID;
  }

  HTTPCodec::StreamID sendHeader() {
    return sendRequest("/", 0, false);
  }

  Promise<Unit> sendRequestLater(HTTPMessage req, bool eof=false) {
    Promise<Unit> reqp;
    reqp.getFuture().then(&eventBase_, [=] {
        sendRequest(req);
        transport_->addReadEvent(requests_, milliseconds(0));
        if (eof) {
          transport_->addReadEOF(milliseconds(0));
        }
      });
    return reqp;
  }

  void SetUp() override {
    folly::EventBaseManager::get()->clearEventBase();
    HTTPSession::setDefaultWriteBufferLimit(65536);
    HTTP2PriorityQueue::setNodeLifetime(std::chrono::milliseconds(2));
  }

  void cleanup() {
    EXPECT_CALL(mockController_, detachSession(_));
    httpSession_->shutdownTransportWithReset(kErrorConnectionReset);
  }


  std::unique_ptr<testing::StrictMock<MockHTTPHandler>>
  addSimpleStrictHandler() {
    std::unique_ptr<testing::StrictMock<MockHTTPHandler>> handler =
      folly::make_unique<testing::StrictMock<MockHTTPHandler>>();

    // The ownership model here is suspect, but assume the callers won't destroy
    // handler before it's requested
    auto rawHandler = handler.get();
    EXPECT_CALL(mockController_, getRequestHandler(testing::_, testing::_))
      .WillOnce(testing::Return(rawHandler))
      .RetiresOnSaturation();

    EXPECT_CALL(*handler, setTransaction(testing::_))
      .WillOnce(testing::SaveArg<0>(&handler->txn_));

    return handler;
  }

  std::unique_ptr<testing::NiceMock<MockHTTPHandler>>
  addSimpleNiceHandler() {
    std::unique_ptr<testing::NiceMock<MockHTTPHandler>> handler =
      folly::make_unique<testing::NiceMock<MockHTTPHandler>>();

    // See comment above
    auto rawHandler = handler.get();
    EXPECT_CALL(mockController_, getRequestHandler(testing::_, testing::_))
      .WillOnce(testing::Return(rawHandler))
      .RetiresOnSaturation();

    EXPECT_CALL(*handler, setTransaction(testing::_))
      .WillOnce(testing::SaveArg<0>(&handler->txn_));

    return handler;
  }

  void onEOMTerminateHandlerExpectShutdown(MockHTTPHandler& handler) {
    handler.expectEOM([&] { handler.terminate(); });
    handler.expectDetachTransaction();
    expectDetachSession();
  }

  void expectDetachSession() {
    EXPECT_CALL(mockController_, detachSession(testing::_));
  }

  void addSingleByteReads(const char* data, milliseconds delay={}) {
    for (const char* p = data; *p != '\0'; ++p) {
      transport_->addReadEvent(p, 1, delay);
    }
  }

  void flushRequestsAndLoop(
    bool eof=false, milliseconds eofDelay=milliseconds(0),
    milliseconds initialDelay=milliseconds(0),
    std::function<void()> extraEventsFn = std::function<void()>()) {
    flushRequests(eof, eofDelay, initialDelay, extraEventsFn);
    eventBase_.loop();
  }

  void flushRequestsAndLoopN(uint64_t n,
    bool eof=false, milliseconds eofDelay=milliseconds(0),
    milliseconds initialDelay=milliseconds(0),
    std::function<void()> extraEventsFn = std::function<void()>()) {
    flushRequests(eof, eofDelay, initialDelay, extraEventsFn);
    for (uint64_t i = 0; i < n; i++) {
      eventBase_.loopOnce();
    }
  }

  void flushRequests(
    bool eof=false, milliseconds eofDelay=milliseconds(0),
    milliseconds initialDelay=milliseconds(0),
    std::function<void()> extraEventsFn = std::function<void()>()) {
    transport_->addReadEvent(requests_, initialDelay);
    if (extraEventsFn) {
      extraEventsFn();
    }
    if (eof) {
      transport_->addReadEOF(eofDelay);
    }
    transport_->startReadEvents();
  }

  void testSimpleUpgrade(
    const std::string& upgradeHeader,
    CodecProtocol expectedProtocol,
    const std::string& expectedUpgradeHeader);

  void gracefulShutdown() {
    folly::DelayedDestruction::DestructorGuard g(httpSession_);
    clientCodec_->generateGoaway(this->requests_, 0, ErrorCode::NO_ERROR);
    expectDetachSession();
    flushRequestsAndLoop(true);
  }

  void testPriorities(uint32_t numPriorities);

  void testChunks(bool trailers);

  void expect101(CodecProtocol expectedProtocol,
                 const std::string& expectedUpgrade,
                 bool expect100 = false) {
    NiceMock<MockHTTPCodecCallback> callbacks;

    EXPECT_CALL(callbacks, onMessageBegin(_, _));
    EXPECT_CALL(callbacks, onNativeProtocolUpgrade(_, _, _, _))
      .WillOnce(
        Invoke([this, expectedUpgrade] (HTTPCodec::StreamID,
                                        CodecProtocol,
                                        const std::string&,
                                        HTTPMessage& msg) {
             EXPECT_EQ(msg.getStatusCode(), 101);
             EXPECT_EQ(msg.getStatusMessage(), "Switching Protocols");
             EXPECT_EQ(msg.getHeaders().getSingleOrEmpty(HTTP_HEADER_UPGRADE),
                       expectedUpgrade);
             // also connection and date
             EXPECT_EQ(msg.getHeaders().size(), 3);
             breakParseOutput_ = true;
             return true;
               }));
    // this comes before 101, but due to gmock this is backwards
    if (expect100) {
      EXPECT_CALL(callbacks, onMessageBegin(_, _))
        .RetiresOnSaturation();
      EXPECT_CALL(callbacks, onHeadersComplete(_, _))
        .WillOnce(Invoke([] (HTTPCodec::StreamID,
                             std::shared_ptr<HTTPMessage> msg) {
                 LOG(INFO) << "100 headers";
                 EXPECT_EQ(msg->getStatusCode(), 100);
                         }))
        .RetiresOnSaturation();
      EXPECT_CALL(callbacks, onMessageComplete(_, _))
        .RetiresOnSaturation();
    }
    clientCodec_->setCallback(&callbacks);
    parseOutput(*clientCodec_);
    clientCodec_ = HTTPCodecFactory::getCodec(expectedProtocol,
                                              TransportDirection::UPSTREAM);
  }

  void expectResponse(uint32_t code = 200,
                      ErrorCode errorCode = ErrorCode::NO_ERROR,
                      bool expect100 = false, bool expectGoaway = false) {
    NiceMock<MockHTTPCodecCallback> callbacks;
    clientCodec_->setCallback(&callbacks);
    if (isParallelCodecProtocol(clientCodec_->getProtocol())) {
      EXPECT_CALL(callbacks, onSettings(_))
        .WillOnce(Invoke([this] (const SettingsList& settings) {
              if (flowControl_[0] > 0) {
                bool foundInitialWindow = false;
                for (const auto& setting: settings) {
                  if (setting.id == SettingsId::INITIAL_WINDOW_SIZE) {
                    EXPECT_EQ(flowControl_[0], setting.value);
                    foundInitialWindow = true;
                  }
                }
                EXPECT_TRUE(foundInitialWindow);
              }
            }));
    }
    if (flowControl_[2] > 0) {
      int64_t sessionDelta =
        flowControl_[2] - clientCodec_->getDefaultWindowSize();
      if (clientCodec_->supportsSessionFlowControl() && sessionDelta) {
        EXPECT_CALL(callbacks, onWindowUpdate(0, sessionDelta));
      }
    }
    if (flowControl_[1] > 0) {
      size_t initWindow = flowControl_[0] > 0 ?
        flowControl_[0] : clientCodec_->getDefaultWindowSize();
      int64_t streamDelta = flowControl_[1] - initWindow;
      if (clientCodec_->supportsStreamFlowControl() && streamDelta) {
        EXPECT_CALL(callbacks, onWindowUpdate(1, streamDelta));
      }
    }

    if (expectGoaway) {
      EXPECT_CALL(callbacks, onGoaway(HTTPCodec::StreamID(1),
                                      ErrorCode::NO_ERROR, _));
    }

    uint8_t times = (expect100) ? 2 : 1;
    EXPECT_CALL(callbacks, onMessageBegin(_, _))
      .Times(times);
    EXPECT_CALL(callbacks, onHeadersComplete(_, _))
      .WillOnce(Invoke([code] (HTTPCodec::StreamID,
                               std::shared_ptr<HTTPMessage> msg) {
                         EXPECT_EQ(msg->getStatusCode(), code);
                       }));
    if (expect100) {
      EXPECT_CALL(callbacks, onHeadersComplete(_, _))
        .WillOnce(Invoke([] (HTTPCodec::StreamID,
                             std::shared_ptr<HTTPMessage> msg) {
                           EXPECT_EQ(msg->getStatusCode(), 100);
                         }))
        .RetiresOnSaturation();
    }
    if (errorCode != ErrorCode::NO_ERROR) {
      EXPECT_CALL(callbacks, onAbort(_, _))
        .WillOnce(Invoke([errorCode] (HTTPCodec::StreamID,
                                      ErrorCode error) {
                           EXPECT_EQ(error, errorCode);
                         }));
    }
    EXPECT_CALL(callbacks, onBody(_, _, _));
    EXPECT_CALL(callbacks, onMessageComplete(_, _));
    parseOutput(*clientCodec_);
  }

  void parseOutput(HTTPCodec& clientCodec) {
    auto writeEvents = transport_->getWriteEvents();
    while (!breakParseOutput_ &&
           (!writeEvents->empty() || !parseOutputStream_.empty())) {
      if (!writeEvents->empty()) {
        auto event = writeEvents->front();
        auto vec = event->getIoVec();
        for (size_t i = 0; i < event->getCount(); i++) {
          parseOutputStream_.append(
            IOBuf::copyBuffer(vec[i].iov_base, vec[i].iov_len));
        }
        writeEvents->pop_front();
      }
      uint32_t consumed = clientCodec.onIngress(*parseOutputStream_.front());
      parseOutputStream_.split(consumed);
    }
    if (!breakParseOutput_) {
      EXPECT_EQ(parseOutputStream_.chainLength(), 0);
    }
    breakParseOutput_ = false;
  }

  void resumeWritesInLoop() {
    eventBase_.runInLoop([this] { transport_->resumeWrites(); });
  }

  void resumeWritesAfterDelay(milliseconds delay) {
    eventBase_.runAfterDelay([this] { transport_->resumeWrites(); },
                             delay.count());
  }

 protected:
  EventBase eventBase_;
  TestAsyncTransport* transport_;  // invalid once httpSession_ is destroyed
  folly::HHWheelTimer::UniquePtr transactionTimeouts_;
  std::vector<int64_t> flowControl_;
  StrictMock<MockController> mockController_;
  HTTPDownstreamSession* httpSession_;
  IOBufQueue requests_{IOBufQueue::cacheChainLength()};
  unique_ptr<HTTPCodec> clientCodec_;
  NiceMock<MockHTTPCodecCallback> callbacks_;
  IOBufQueue parseOutputStream_{IOBufQueue::cacheChainLength()};
  bool breakParseOutput_{false};
};

// Uses TestAsyncTransport
typedef HTTPDownstreamTest<HTTP1xCodecPair> HTTPDownstreamSessionTest;
typedef HTTPDownstreamTest<SPDY3CodecPair> SPDY3DownstreamSessionTest;
namespace {
class HTTP2DownstreamSessionTest : public HTTPDownstreamTest<HTTP2CodecPair> {
 public:
  HTTP2DownstreamSessionTest()
      : HTTPDownstreamTest<HTTP2CodecPair>() {}

  void SetUp() override {
    HTTPDownstreamTest<HTTP2CodecPair>::SetUp();
    HTTP2Codec::setHeaderSplitSize(http2::kMaxFramePayloadLengthMin);
  }

  void TearDown() override {
    HTTP2Codec::setHeaderSplitSize(http2::kMaxFramePayloadLengthMin);
  }
};
}

TEST_F(HTTPDownstreamSessionTest, immediate_eof) {
  // Send EOF without any request data
  EXPECT_CALL(mockController_, getRequestHandler(_, _)).Times(0);
  expectDetachSession();

  flushRequestsAndLoop(true, milliseconds(0));
}

TEST_F(HTTPDownstreamSessionTest, http_1_0_no_headers) {
  InSequence enforceOrder;

  auto handler = addSimpleNiceHandler();
  handler->expectHeaders([&] (std::shared_ptr<HTTPMessage> msg) {
      EXPECT_FALSE(msg->getIsChunked());
      EXPECT_FALSE(msg->getIsUpgraded());
      EXPECT_EQ("/", msg->getURL());
      EXPECT_EQ("/", msg->getPath());
      EXPECT_EQ("", msg->getQueryString());
      EXPECT_EQ(1, msg->getHTTPVersion().first);
      EXPECT_EQ(0, msg->getHTTPVersion().second);
    });
  onEOMTerminateHandlerExpectShutdown(*handler);

  auto req = getGetRequest();
  req.setHTTPVersion(1, 0);
  sendRequest(req);
  flushRequestsAndLoop();
}

TEST_F(HTTPDownstreamSessionTest, http_1_0_no_headers_eof) {
  InSequence enforceOrder;

  auto handler = addSimpleNiceHandler();
  handler->expectHeaders([&] (std::shared_ptr<HTTPMessage> msg) {
      EXPECT_FALSE(msg->getIsChunked());
      EXPECT_FALSE(msg->getIsUpgraded());
      EXPECT_EQ("http://example.com/foo?bar", msg->getURL());
      EXPECT_EQ("/foo", msg->getPath());
      EXPECT_EQ("bar", msg->getQueryString());
      EXPECT_EQ(1, msg->getHTTPVersion().first);
      EXPECT_EQ(0, msg->getHTTPVersion().second);
    });
  onEOMTerminateHandlerExpectShutdown(*handler);

  const char *req = "GET http://example.com/foo?bar HTTP/1.0\r\n\r\n";
  requests_.append(req, strlen(req));
  flushRequestsAndLoop(true, milliseconds(0));
}

TEST_F(HTTPDownstreamSessionTest, single_bytes) {
  InSequence enforceOrder;

  auto handler = addSimpleNiceHandler();
  handler->expectHeaders([&] (std::shared_ptr<HTTPMessage> msg) {
      const HTTPHeaders& hdrs = msg->getHeaders();
      EXPECT_EQ(2, hdrs.size());
      EXPECT_TRUE(hdrs.exists("host"));
      EXPECT_TRUE(hdrs.exists("connection"));

      EXPECT_FALSE(msg->getIsChunked());
      EXPECT_FALSE(msg->getIsUpgraded());
      EXPECT_EQ("/somepath.php?param=foo", msg->getURL());
      EXPECT_EQ("/somepath.php", msg->getPath());
      EXPECT_EQ("param=foo", msg->getQueryString());
      EXPECT_EQ(1, msg->getHTTPVersion().first);
      EXPECT_EQ(1, msg->getHTTPVersion().second);
    });
  onEOMTerminateHandlerExpectShutdown(*handler);

  addSingleByteReads("GET /somepath.php?param=foo HTTP/1.1\r\n"
                     "Host: example.com\r\n"
                     "Connection: close\r\n"
                     "\r\n");
  transport_->addReadEOF(milliseconds(0));
  transport_->startReadEvents();
  eventBase_.loop();
}

TEST_F(HTTPDownstreamSessionTest, single_bytes_with_body) {
  InSequence enforceOrder;

  auto handler = addSimpleNiceHandler();
  handler->expectHeaders([&] (std::shared_ptr<HTTPMessage> msg) {
      const HTTPHeaders& hdrs = msg->getHeaders();
      EXPECT_EQ(3, hdrs.size());
      EXPECT_TRUE(hdrs.exists("host"));
      EXPECT_TRUE(hdrs.exists("content-length"));
      EXPECT_TRUE(hdrs.exists("myheader"));

      EXPECT_FALSE(msg->getIsChunked());
      EXPECT_FALSE(msg->getIsUpgraded());
      EXPECT_EQ("/somepath.php?param=foo", msg->getURL());
      EXPECT_EQ("/somepath.php", msg->getPath());
      EXPECT_EQ("param=foo", msg->getQueryString());
      EXPECT_EQ(1, msg->getHTTPVersion().first);
      EXPECT_EQ(1, msg->getHTTPVersion().second);
    });
  EXPECT_CALL(*handler, onBody(_))
    .WillOnce(ExpectString("1"))
    .WillOnce(ExpectString("2"))
    .WillOnce(ExpectString("3"))
    .WillOnce(ExpectString("4"))
    .WillOnce(ExpectString("5"));
  onEOMTerminateHandlerExpectShutdown(*handler);

  addSingleByteReads("POST /somepath.php?param=foo HTTP/1.1\r\n"
                     "Host: example.com\r\n"
                     "MyHeader: FooBar\r\n"
                     "Content-Length: 5\r\n"
                     "\r\n"
                     "12345");
  transport_->addReadEOF(milliseconds(0));
  transport_->startReadEvents();
  eventBase_.loop();
}

TEST_F(HTTPDownstreamSessionTest, split_body) {
  InSequence enforceOrder;

  auto handler = addSimpleNiceHandler();
  handler->expectHeaders([&] (std::shared_ptr<HTTPMessage> msg) {
      const HTTPHeaders& hdrs = msg->getHeaders();
      EXPECT_EQ(2, hdrs.size());
    });
  EXPECT_CALL(*handler, onBody(_))
    .WillOnce(ExpectString("12345"))
    .WillOnce(ExpectString("abcde"));
  onEOMTerminateHandlerExpectShutdown(*handler);

  transport_->addReadEvent("POST / HTTP/1.1\r\n"
                           "Host: example.com\r\n"
                           "Content-Length: 10\r\n"
                           "\r\n"
                           "12345", milliseconds(0));
  transport_->addReadEvent("abcde", milliseconds(5));
  transport_->addReadEOF(milliseconds(0));
  transport_->startReadEvents();
  eventBase_.loop();
}

TEST_F(HTTPDownstreamSessionTest, post_chunked) {
  InSequence enforceOrder;

  auto handler = addSimpleNiceHandler();
  handler->expectHeaders([&] (std::shared_ptr<HTTPMessage> msg) {
      const HTTPHeaders& hdrs = msg->getHeaders();
      EXPECT_EQ(3, hdrs.size());
      EXPECT_TRUE(hdrs.exists("host"));
      EXPECT_TRUE(hdrs.exists("content-type"));
      EXPECT_TRUE(hdrs.exists("transfer-encoding"));
      EXPECT_TRUE(msg->getIsChunked());
      EXPECT_FALSE(msg->getIsUpgraded());
      EXPECT_EQ("http://example.com/cgi-bin/foo.aspx?abc&def",
                msg->getURL());
      EXPECT_EQ("/cgi-bin/foo.aspx", msg->getPath());
      EXPECT_EQ("abc&def", msg->getQueryString());
      EXPECT_EQ(1, msg->getHTTPVersion().first);
      EXPECT_EQ(1, msg->getHTTPVersion().second);
    });
  EXPECT_CALL(*handler, onChunkHeader(3));
  EXPECT_CALL(*handler, onBody(_))
    .WillOnce(ExpectString("bar"));
  EXPECT_CALL(*handler, onChunkComplete());
  EXPECT_CALL(*handler, onChunkHeader(0x22));
  EXPECT_CALL(*handler, onBody(_))
    .WillOnce(ExpectString("0123456789abcdef\nfedcba9876543210\n"));
  EXPECT_CALL(*handler, onChunkComplete());
  EXPECT_CALL(*handler, onChunkHeader(3));
  EXPECT_CALL(*handler, onBody(_))
    .WillOnce(ExpectString("foo"));
  EXPECT_CALL(*handler, onChunkComplete());
  onEOMTerminateHandlerExpectShutdown(*handler);

  transport_->addReadEvent("POST http://example.com/cgi-bin/foo.aspx?abc&def "
                           "HTTP/1.1\r\n"
                           "Host: example.com\r\n"
                           "Content-Type: text/pla", milliseconds(0));
  transport_->addReadEvent("in; charset=utf-8\r\n"
                           "Transfer-encoding: chunked\r\n"
                           "\r", milliseconds(2));
  transport_->addReadEvent("\n"
                           "3\r\n"
                           "bar\r\n"
                           "22\r\n"
                           "0123456789abcdef\n"
                           "fedcba9876543210\n"
                           "\r\n"
                           "3\r", milliseconds(3));
  transport_->addReadEvent("\n"
                           "foo\r\n"
                           "0\r\n\r\n", milliseconds(1));
  transport_->startReadEvents();
  eventBase_.loop();
}

TEST_F(HTTPDownstreamSessionTest, multi_message) {
  InSequence enforceOrder;

  auto handler1 = addSimpleNiceHandler();
  handler1->expectHeaders();
  EXPECT_CALL(*handler1, onBody(_))
    .WillOnce(ExpectString("foo"))
    .WillOnce(ExpectString("bar9876"));
  handler1->expectEOM([&] { handler1->sendReply(); });
  handler1->expectDetachTransaction();

  auto handler2 = addSimpleNiceHandler();
  handler2->expectHeaders();
  EXPECT_CALL(*handler2, onChunkHeader(0xa));
  EXPECT_CALL(*handler2, onBody(_))
    .WillOnce(ExpectString("some "))
    .WillOnce(ExpectString("data\n"));
  EXPECT_CALL(*handler2, onChunkComplete());
  onEOMTerminateHandlerExpectShutdown(*handler2);

  transport_->addReadEvent("POST / HTTP/1.1\r\n"
                           "Host: example.com\r\n"
                           "Content-Length: 10\r\n"
                           "\r\n"
                           "foo", milliseconds(0));
  transport_->addReadEvent("bar9876"
                           "POST /foo HTTP/1.1\r\n"
                           "Host: exa", milliseconds(2));
  transport_->addReadEvent("mple.com\r\n"
                           "Connection: close\r\n"
                           "Trans", milliseconds(0));
  transport_->addReadEvent("fer-encoding: chunked\r\n"
                           "\r\n", milliseconds(2));
  transport_->addReadEvent("a\r\nsome ", milliseconds(0));
  transport_->addReadEvent("data\n\r\n0\r\n\r\n", milliseconds(2));
  transport_->addReadEOF(milliseconds(0));
  transport_->startReadEvents();
  eventBase_.loop();
}

TEST_F(HTTPDownstreamSessionTest, connect) {
  InSequence enforceOrder;

  auto handler = addSimpleStrictHandler();
  // Send HTTP 200 OK to accept the CONNECT request
  handler->expectHeaders([&handler] {
      handler->sendHeaders(200, 100);
    });

  EXPECT_CALL(*handler, onUpgrade(_));

  // Data should be received using onBody
  EXPECT_CALL(*handler, onBody(_))
    .WillOnce(ExpectString("12345"))
    .WillOnce(ExpectString("abcde"));
  onEOMTerminateHandlerExpectShutdown(*handler);

  transport_->addReadEvent("CONNECT test HTTP/1.1\r\n"
                           "\r\n"
                           "12345", milliseconds(0));
  transport_->addReadEvent("abcde", milliseconds(5));
  transport_->addReadEOF(milliseconds(0));
  transport_->startReadEvents();
  eventBase_.loop();
}

TEST_F(HTTPDownstreamSessionTest, connect_rejected) {
  InSequence enforceOrder;

  auto handler = addSimpleStrictHandler();
  // Send HTTP 400 to reject the CONNECT request
  handler->expectHeaders([&handler] {
      handler->sendReplyCode(400);
    });

  onEOMTerminateHandlerExpectShutdown(*handler);

  transport_->addReadEvent("CONNECT test HTTP/1.1\r\n"
                           "\r\n"
                           "12345", milliseconds(0));
  transport_->addReadEvent("abcde", milliseconds(5));
  transport_->addReadEOF(milliseconds(0));
  transport_->startReadEvents();
  eventBase_.loop();
}

TEST_F(HTTPDownstreamSessionTest, http_upgrade) {
  InSequence enforceOrder;

  auto handler = addSimpleStrictHandler();
  // Send HTTP 101 Switching Protocls to accept the upgrade request
  handler->expectHeaders([&handler] {
      handler->sendHeaders(101, 100);
    });

  // Send the response in the new protocol after upgrade
  EXPECT_CALL(*handler, onUpgrade(_))
    .WillOnce(Invoke([&handler] (UpgradeProtocol protocol) {
          handler->sendReplyCode(100);
        }));

  onEOMTerminateHandlerExpectShutdown(*handler);

  HTTPMessage req = getGetRequest();
  req.getHeaders().add(HTTP_HEADER_UPGRADE, "TEST/1.0");
  req.getHeaders().add(HTTP_HEADER_CONNECTION, "upgrade");
  sendRequest(req);
  flushRequestsAndLoop(true, milliseconds(0));
}

TEST(HTTPDownstreamTest, parse_error_no_txn) {
  // 1) Get a parse error on SYN_STREAM for streamID == 1
  // 2) Expect that the codec should be asked to generate an abort on
  //    streamID==1
  EventBase evb;

  // Setup the controller and its expecations.
  NiceMock<MockController> mockController;

  // Setup the codec, its callbacks, and its expectations.
  auto codec = makeDownstreamParallelCodec();
  HTTPCodec::Callback* codecCallback = nullptr;
  EXPECT_CALL(*codec, setCallback(_))
    .WillRepeatedly(SaveArg<0>(&codecCallback));
  // Expect egress abort for streamID == 1
  EXPECT_CALL(*codec, generateRstStream(_, 1, _));

  // Setup transport
  bool transportGood = true;
  auto transport = newMockTransport(&evb);
  EXPECT_CALL(*transport, good())
    .WillRepeatedly(ReturnPointee(&transportGood));
  EXPECT_CALL(*transport, closeNow())
    .WillRepeatedly(Assign(&transportGood, false));
  EXPECT_CALL(*transport, writeChain(_, _, _))
    .WillRepeatedly(
      Invoke([&] (folly::AsyncTransportWrapper::WriteCallback* callback,
                  const shared_ptr<IOBuf>&, WriteFlags) {
               callback->writeSuccess();
             }));

  // Create the downstream session, thus initializing codecCallback
  auto transactionTimeouts = makeInternalTimeoutSet(&evb);
  auto session = new HTTPDownstreamSession(
    transactionTimeouts.get(),
    AsyncTransportWrapper::UniquePtr(transport),
    localAddr, peerAddr,
    &mockController, std::move(codec),
    mockTransportInfo);
  session->startNow();
  HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS, "foo");
  ex.setProxygenError(kErrorParseHeader);
  ex.setCodecStatusCode(ErrorCode::REFUSED_STREAM);
  codecCallback->onError(HTTPCodec::StreamID(1), ex, true);

  // cleanup
  session->shutdownTransportWithReset(kErrorConnectionReset);
  evb.loop();
}

TEST(HTTPDownstreamTest, byte_events_drained) {
  // Test that byte events are drained before socket is closed
  EventBase evb;

  NiceMock<MockController> mockController;
  auto codec = makeDownstreamParallelCodec();
  auto byteEventTracker = new MockByteEventTracker(nullptr);
  auto transport = newMockTransport(&evb);
  auto transactionTimeouts = makeInternalTimeoutSet(&evb);

  // Create the downstream session
  auto session = new HTTPDownstreamSession(
    transactionTimeouts.get(),
    AsyncTransportWrapper::UniquePtr(transport),
    localAddr, peerAddr,
    &mockController, std::move(codec),
    mockTransportInfo);
  session->setByteEventTracker(
      std::unique_ptr<ByteEventTracker>(byteEventTracker));

  InSequence enforceOrder;

  session->startNow();

  // Byte events should be drained first
  EXPECT_CALL(*byteEventTracker, drainByteEvents())
    .Times(1);
  EXPECT_CALL(*transport, closeWithReset())
    .Times(AtLeast(1));

  // Close the socket
  session->shutdownTransportWithReset(kErrorConnectionReset);
  evb.loop();
}

TEST_F(HTTP2DownstreamSessionTest, set_byte_event_tracker) {
  InSequence enforceOrder;

  // Send two requests with writes paused, which will queue several byte events,
  // including last byte events which are holding a reference to the
  // transaction.
  transport_->pauseWrites();
  auto handler1 = addSimpleStrictHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&handler1] () {
      handler1->sendReplyWithBody(200, 100);
    });
  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders();
  handler2->expectEOM([&handler2] () {
      handler2->sendReplyWithBody(200, 100);
    });

  sendRequest();
  sendRequest();
  // Resume writes from the loop callback
  eventBase_.runInLoop([this] {
      transport_->resumeWrites();
    });

  // Graceful shutdown will notify of GOAWAY
  EXPECT_CALL(*handler1, onGoaway(ErrorCode::NO_ERROR));
  EXPECT_CALL(*handler2, onGoaway(ErrorCode::NO_ERROR));
  // The original byteEventTracker will process the last byte event of the
  // first transaction, and detach by deleting the event.  Swap out the tracker.
  handler1->expectDetachTransaction([this] {
      auto tracker = folly::make_unique<ByteEventTracker>(httpSession_);
      httpSession_->setByteEventTracker(std::move(tracker));
    });
  // handler2 should also be detached immediately because the new
  // ByteEventTracker continues procesing where the old one left off.
  handler2->expectDetachTransaction();
  gracefulShutdown();
}

TEST_F(HTTPDownstreamSessionTest, trailers) {
  testChunks(true);
}

TEST_F(HTTPDownstreamSessionTest, explicit_chunks) {
  testChunks(false);
}

template <class C>
void HTTPDownstreamTest<C>::testChunks(bool trailers) {
  InSequence enforceOrder;

  auto handler = addSimpleStrictHandler();
  handler->expectHeaders();
  handler->expectEOM([&handler, trailers] () {
      handler->sendChunkedReplyWithBody(200, 100, 17, trailers);
    });
  handler->expectDetachTransaction();

  HTTPSession::DestructorGuard g(httpSession_);
  sendRequest();
  flushRequestsAndLoop(true, milliseconds(0));

  EXPECT_CALL(callbacks_, onMessageBegin(1, _))
    .Times(1);
  EXPECT_CALL(callbacks_, onHeadersComplete(1, _))
    .Times(1);
  for (int i = 0; i < 6; i++) {
    EXPECT_CALL(callbacks_, onChunkHeader(1, _));
    EXPECT_CALL(callbacks_, onBody(1, _, _));
    EXPECT_CALL(callbacks_, onChunkComplete(1));
  }
  if (trailers) {
    EXPECT_CALL(callbacks_, onTrailersComplete(1, _));
  }
  EXPECT_CALL(callbacks_, onMessageComplete(1, _));

  parseOutput(*clientCodec_);
  expectDetachSession();
}

TEST_F(HTTPDownstreamSessionTest, http_drain) {
  InSequence enforceOrder;

  auto handler1 = addSimpleStrictHandler();
  handler1->expectHeaders([this, &handler1] {
      handler1->sendHeaders(200, 100);
      httpSession_->notifyPendingShutdown();
    });
  handler1->expectEOM([&handler1] {
      handler1->sendBody(100);
      handler1->txn_->sendEOM();
    });
  handler1->expectDetachTransaction();

  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders([this, &handler2] {
      handler2->sendHeaders(200, 100);
    });
  handler2->expectEOM([&handler2] {
          handler2->sendBody(100);
          handler2->txn_->sendEOM();
    });
  handler2->expectDetachTransaction();

  expectDetachSession();

  sendRequest();
  sendRequest();
  flushRequestsAndLoop();
}

// 1) receive full request
// 2) notify pending shutdown
// 3) wait for session read timeout -> should be ignored
// 4) response completed
TEST_F(HTTPDownstreamSessionTest, http_drain_long_running) {
  InSequence enforceSequence;

  auto handler = addSimpleStrictHandler();
  handler->expectHeaders([this, &handler] {
      httpSession_->notifyPendingShutdown();
      eventBase_.tryRunAfterDelay([this] {
          // simulate read timeout
          httpSession_->timeoutExpired();
        }, 100);
      eventBase_.tryRunAfterDelay([&handler] {
          handler->sendReplyWithBody(200, 100);
        }, 200);
    });
  handler->expectEOM();
  handler->expectDetachTransaction();

  expectDetachSession();

  sendRequest();
  flushRequestsAndLoop();
}

TEST_F(HTTPDownstreamSessionTest, early_abort) {
  StrictMock<MockHTTPHandler> handler;

  InSequence enforceOrder;
  EXPECT_CALL(mockController_, getRequestHandler(_, _))
    .WillOnce(Return(&handler));

  EXPECT_CALL(handler, setTransaction(_))
    .WillOnce(Invoke([&] (HTTPTransaction* txn) {
          handler.txn_ = txn;
          handler.txn_->sendAbort();
        }));
  handler.expectDetachTransaction();
  expectDetachSession();

  addSingleByteReads("GET /somepath.php?param=foo HTTP/1.1\r\n"
                     "Host: example.com\r\n"
                     "Connection: close\r\n"
                     "\r\n");
  transport_->addReadEOF(milliseconds(0));
  transport_->startReadEvents();
  eventBase_.loop();
}

TEST_F(SPDY3DownstreamSessionTest, http_paused_buffered) {
  IOBufQueue rst{IOBufQueue::cacheChainLength()};
  auto s = sendRequest();
  clientCodec_->generateRstStream(rst, s, ErrorCode::CANCEL);
  sendRequest();

  InSequence handlerSequence;
  auto handler1 = addSimpleNiceHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&handler1, this] {
      transport_->pauseWrites();
      handler1->sendHeaders(200, 65536 * 2);
      handler1->sendBody(65536 * 2);
    });
  handler1->expectEgressPaused();
  auto handler2 = addSimpleNiceHandler();
  handler2->expectEgressPaused();
  handler2->expectHeaders();
  handler2->expectEOM([&] {
      eventBase_.runInLoop([&] {
          transport_->addReadEvent(rst, milliseconds(0)); });
    });
  handler1->expectError([&] (const HTTPException& ex) {
      ASSERT_EQ(ex.getProxygenError(), kErrorStreamAbort);
      resumeWritesInLoop();
    });
  handler1->expectDetachTransaction();
  handler2->expectEgressResumed([&] {
      handler2->sendReplyWithBody(200, 32768);
    });
  handler2->expectDetachTransaction([this] {
      eventBase_.runInLoop([&] { transport_->addReadEOF(milliseconds(0)); });
    });
  expectDetachSession();

  flushRequestsAndLoop();
}

TEST_F(HTTPDownstreamSessionTest, http_writes_draining_timeout) {
  sendRequest();
  sendHeader();

  InSequence handlerSequence;
  auto handler1 = addSimpleNiceHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&handler1, this] {
      transport_->pauseWrites();
      handler1->sendHeaders(200, 1000);
    });
  handler1->expectError([&] (const HTTPException& ex) {
      ASSERT_EQ(ex.getProxygenError(), kErrorWriteTimeout);
      ASSERT_EQ(
        folly::to<std::string>("WriteTimeout on transaction id: ",
                               handler1->txn_->getID()),
        std::string(ex.what()));
      handler1->txn_->sendAbort();
    });
  handler1->expectDetachTransaction();
  expectDetachSession();

  flushRequestsAndLoop();
}

TEST_F(HTTPDownstreamSessionTest, http_rate_limit_normal) {
  // The rate-limiting code grabs the event base from the EventBaseManager,
  // so we need to set it.
  folly::EventBaseManager::get()->setEventBase(&eventBase_, false);

  // Create a request
  sendRequest();

  InSequence handlerSequence;

  // Set a low rate-limit on the transaction
  auto handler1 = addSimpleNiceHandler();
  handler1->expectHeaders([&] {
      uint32_t rateLimit_kbps = 640;
      handler1->txn_->setEgressRateLimit(rateLimit_kbps * 1024);
    });
  // Send a somewhat big response that we know will get rate-limited
  handler1->expectEOM([&handler1, this] {
      // At 640kbps, this should take slightly over 800ms
      uint32_t rspLengthBytes = 100000;
      handler1->sendHeaders(200, rspLengthBytes);
      handler1->sendBody(rspLengthBytes);
      handler1->txn_->sendEOM();
    });
  handler1->expectDetachTransaction();

  // Keep the session around even after the event base loop completes so we can
  // read the counters on a valid object.
  HTTPSession::DestructorGuard g(httpSession_);
  flushRequestsAndLoop();

  proxygen::TimePoint timeFirstWrite =
    transport_->getWriteEvents()->front()->getTime();
  proxygen::TimePoint timeLastWrite =
    transport_->getWriteEvents()->back()->getTime();
  int64_t writeDuration =
    (int64_t)millisecondsBetween(timeLastWrite, timeFirstWrite).count();
  EXPECT_GE(writeDuration, 800);

  cleanup();
}

TEST_F(SPDY3DownstreamSessionTest, spdy_rate_limit_normal) {
  // The rate-limiting code grabs the event base from the EventBaseManager,
  // so we need to set it.
  folly::EventBaseManager::get()->setEventBase(&eventBase_, false);

  clientCodec_->getEgressSettings()->setSetting(SettingsId::INITIAL_WINDOW_SIZE,
                                                100000);
  clientCodec_->generateSettings(requests_);
  sendRequest();

  InSequence handlerSequence;
  auto handler1 = addSimpleNiceHandler();
  handler1->expectHeaders([&] {
      uint32_t rateLimit_kbps = 640;
      handler1->txn_->setEgressRateLimit(rateLimit_kbps * 1024);
    });

  handler1->expectEOM([&handler1, this] {
      // At 640kbps, this should take slightly over 800ms
      uint32_t rspLengthBytes = 100000;
      handler1->sendHeaders(200, rspLengthBytes);
      handler1->sendBody(rspLengthBytes);
      handler1->txn_->sendEOM();
    });
  handler1->expectDetachTransaction();

  // Keep the session around even after the event base loop completes so we can
  // read the counters on a valid object.
  HTTPSession::DestructorGuard g(httpSession_);
  flushRequestsAndLoop(true, milliseconds(50));

  proxygen::TimePoint timeFirstWrite =
    transport_->getWriteEvents()->front()->getTime();
  proxygen::TimePoint timeLastWrite =
    transport_->getWriteEvents()->back()->getTime();
  int64_t writeDuration =
    (int64_t)millisecondsBetween(timeLastWrite, timeFirstWrite).count();
  EXPECT_GE(writeDuration, 800);
  expectDetachSession();
}

/**
 * This test will reset the connection while the server is waiting around
 * to send more bytes (so as to keep under the rate limit).
 */
TEST_F(SPDY3DownstreamSessionTest, spdy_rate_limit_rst) {
  // The rate-limiting code grabs the event base from the EventBaseManager,
  // so we need to set it.
  folly::EventBaseManager::get()->setEventBase(&eventBase_, false);

  IOBufQueue rst{IOBufQueue::cacheChainLength()};
  clientCodec_->getEgressSettings()->setSetting(SettingsId::INITIAL_WINDOW_SIZE,
                                                100000);
  clientCodec_->generateSettings(requests_);
  auto streamID = sendRequest();
  clientCodec_->generateRstStream(rst, streamID, ErrorCode::CANCEL);

  InSequence handlerSequence;
  auto handler1 = addSimpleNiceHandler();
  handler1->expectHeaders([&] {
      uint32_t rateLimit_kbps = 640;
      handler1->txn_->setEgressRateLimit(rateLimit_kbps * 1024);
    });
  handler1->expectEOM([&handler1, this] {
      uint32_t rspLengthBytes = 100000;
      handler1->sendHeaders(200, rspLengthBytes);
      handler1->sendBody(rspLengthBytes);
      handler1->txn_->sendEOM();
    });
  handler1->expectError();
  handler1->expectDetachTransaction();
  expectDetachSession();

  flushRequestsAndLoop(true, milliseconds(50), milliseconds(0), [&] {
      transport_->addReadEvent(rst, milliseconds(10));
    });
}

// Send a 1.0 request, egress the EOM with the last body chunk on a paused
// socket, and let it timeout.  shutdownTransportWithReset will result in a call
// to removeTransaction with writesDraining_=true
TEST_F(HTTPDownstreamSessionTest, write_timeout) {
  HTTPMessage req = getGetRequest();
  req.setHTTPVersion(1, 0);
  sendRequest(req);

  InSequence handlerSequence;
  auto handler1 = addSimpleNiceHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&handler1, this] {
      handler1->sendHeaders(200, 100);
      eventBase_.tryRunAfterDelay([&handler1, this] {
          transport_->pauseWrites();
          handler1->sendBody(100);
          handler1->txn_->sendEOM();
        }, 50);
    });
  handler1->expectError([&] (const HTTPException& ex) {
      ASSERT_EQ(ex.getProxygenError(), kErrorWriteTimeout);
      ASSERT_EQ(folly::to<std::string>("WriteTimeout on transaction id: ",
                                       handler1->txn_->getID()),
                std::string(ex.what()));
    });
  handler1->expectDetachTransaction();

  expectDetachSession();

  flushRequestsAndLoop();
}

// Send an abort from the write timeout path while pipelining
TEST_F(HTTPDownstreamSessionTest, write_timeout_pipeline) {
  const char* buf = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"
    "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
  requests_.append(buf, strlen(buf));

  InSequence handlerSequence;
  auto handler1 = addSimpleNiceHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&handler1, this] {
      handler1->sendHeaders(200, 100);
      eventBase_.tryRunAfterDelay([&handler1, this] {
          transport_->pauseWrites();
          handler1->sendBody(100);
          handler1->txn_->sendEOM();
        }, 50);
    });
  handler1->expectError([&] (const HTTPException& ex) {
      ASSERT_EQ(ex.getProxygenError(), kErrorWriteTimeout);
      ASSERT_EQ(folly::to<std::string>("WriteTimeout on transaction id: ",
                                       handler1->txn_->getID()),
                std::string(ex.what()));
      handler1->txn_->sendAbort();
    });
  handler1->expectDetachTransaction();
  expectDetachSession();

  flushRequestsAndLoop();
}

TEST_F(HTTPDownstreamSessionTest, body_packetization) {
  HTTPMessage req = getGetRequest();
  req.setHTTPVersion(1, 0);
  req.setWantsKeepalive(false);
  sendRequest(req);

  InSequence handlerSequence;
  auto handler1 = addSimpleNiceHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&handler1, this] {
      handler1->sendReplyWithBody(200, 32768);
    });
  handler1->expectDetachTransaction();

  expectDetachSession();

  // Keep the session around even after the event base loop completes so we can
  // read the counters on a valid object.
  HTTPSession::DestructorGuard g(httpSession_);
  flushRequestsAndLoop();

  EXPECT_EQ(transport_->getWriteEvents()->size(), 1);
}

TEST_F(HTTPDownstreamSessionTest, http_malformed_pkt1) {
  // Create a HTTP connection and keep sending just '\n' to the HTTP1xCodec.
  std::string data(90000, '\n');
  requests_.append(data.data(), data.length());

  expectDetachSession();

  flushRequestsAndLoop(true, milliseconds(0));
}

TEST_F(HTTPDownstreamSessionTest, big_explcit_chunk_write) {
  // even when the handler does a massive write, the transport only gets small
  // writes
  sendRequest();

  auto handler = addSimpleNiceHandler();
  handler->expectHeaders([&handler] {
      handler->sendHeaders(200, 100, false);
      size_t len = 16 * 1024 * 1024;
      handler->txn_->sendChunkHeader(len);
      auto chunk = makeBuf(len);
      handler->txn_->sendBody(std::move(chunk));
      handler->txn_->sendChunkTerminator();
      handler->txn_->sendEOM();
    });
  handler->expectDetachTransaction();

  expectDetachSession();

  // Keep the session around even after the event base loop completes so we can
  // read the counters on a valid object.
  HTTPSession::DestructorGuard g(httpSession_);
  flushRequestsAndLoop();

  EXPECT_GT(transport_->getWriteEvents()->size(), 250);
}


// ==== upgrade tests ====

// Test upgrade to a protocol unknown to HTTPSession
TEST_F(HTTPDownstreamSessionTest, http_upgrade_non_native) {
  auto handler = addSimpleStrictHandler();

  handler->expectHeaders([this, &handler] {
      handler->sendHeaders(101, 0, true, {{"Upgrade", "blarf"}});
    });
  EXPECT_CALL(*handler, onUpgrade(UpgradeProtocol::TCP));
  handler->expectEOM([this, &handler] {
      handler->txn_->sendEOM();
    });
  handler->expectDetachTransaction();

  sendRequest(getUpgradeRequest("blarf"));
  expectDetachSession();
  flushRequestsAndLoop(true);
}

// Test upgrade to a protocol unknown to HTTPSession, but don't switch
// protocols
TEST_F(HTTPDownstreamSessionTest, http_upgrade_non_native_ignore) {
  auto handler = addSimpleStrictHandler();

  handler->expectHeaders([this, &handler] {
      handler->sendReplyWithBody(200, 100);
    });
  handler->expectEOM();
  handler->expectDetachTransaction();

  sendRequest(getUpgradeRequest("blarf"));

  expectDetachSession();
  flushRequestsAndLoop(true);
}


// Test upgrade to a protocol unknown to HTTPSession
TEST_F(HTTPDownstreamSessionTest, http_upgrade_non_native_pipeline) {
  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders([this, &handler1] (std::shared_ptr<HTTPMessage> msg) {
      EXPECT_EQ(msg->getHeaders().getSingleOrEmpty(HTTP_HEADER_UPGRADE),
                "blarf");
      handler1->sendReplyWithBody(200, 100);
    });
  handler1->expectEOM();
  handler1->expectDetachTransaction();

  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders([this, &handler2] {
      handler2->sendReplyWithBody(200, 100);
    });
  handler2->expectEOM();
  handler2->expectDetachTransaction();

  sendRequest(getUpgradeRequest("blarf"));
  transport_->addReadEvent("GET / HTTP/1.1\r\n"
                           "\r\n");
  expectDetachSession();
  flushRequestsAndLoop(true);
}

// Helper that does a simple upgrade test - request an upgrade, receive a 101
// and an upgraded response
template <class C>
void HTTPDownstreamTest<C>::testSimpleUpgrade(
  const std::string& upgradeHeader,
  CodecProtocol expectedProtocol,
  const std::string& expectedUpgradeHeader) {
  this->getCodec().setAllowedUpgradeProtocols({expectedUpgradeHeader});

  auto handler = addSimpleStrictHandler();

  handler->expectHeaders();
  EXPECT_CALL(mockController_, onSessionCodecChange(httpSession_));
  handler->expectEOM([&handler, expectedUpgradeHeader] {
      EXPECT_FALSE(handler->txn_->getSetupTransportInfo().secure);
      EXPECT_EQ(*handler->txn_->getSetupTransportInfo().appProtocol,
                expectedUpgradeHeader);
      handler->sendReplyWithBody(200, 100);
    });
  handler->expectDetachTransaction();

  HTTPMessage req = getUpgradeRequest(upgradeHeader);
  if (upgradeHeader == http2::kProtocolCleartextString) {
    HTTP2Codec::requestUpgrade(req);
  }
  sendRequest(req);
  flushRequestsAndLoop();

  expect101(expectedProtocol, expectedUpgradeHeader);
  expectResponse();
  gracefulShutdown();
}

// Upgrade to SPDY/3
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_3) {
  testSimpleUpgrade("spdy/3", CodecProtocol::SPDY_3, "spdy/3");
}

// Upgrade to SPDY/3.1
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_3_1) {
  testSimpleUpgrade("spdy/3.1", CodecProtocol::SPDY_3_1, "spdy/3.1");
}

// Upgrade to HTTP/2
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_h2) {
  testSimpleUpgrade("h2c", CodecProtocol::HTTP_2, "h2c");
}

class HTTPDownstreamSessionUpgradeFlowControlTest :
      public HTTPDownstreamSessionTest {
 public:
  HTTPDownstreamSessionUpgradeFlowControlTest()
      : HTTPDownstreamSessionTest({100000, 105000, 110000}) {}
};

// Upgrade to HTTP/2, with non-default flow control settings
TEST_F(HTTPDownstreamSessionUpgradeFlowControlTest, upgrade_h2_flowcontrol) {
  testSimpleUpgrade("h2c", CodecProtocol::HTTP_2, "h2c");
}

// Upgrade to SPDY/3.1 with a non-native proto in the list
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_unknown) {
  // This is maybe weird, the client asked for non-native as first choice,
  // but we go native
  testSimpleUpgrade("blarf, spdy/3.1, spdy/3",
                    CodecProtocol::SPDY_3_1, "spdy/3.1");
}

// Upgrade header with extra whitespace
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_whitespace) {
  testSimpleUpgrade(" \tspdy/3.1\t , spdy/3",
                    CodecProtocol::SPDY_3_1, "spdy/3.1");
}

// Upgrade header with random junk
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_junk) {
  testSimpleUpgrade(",,,,   ,,\t~^%$(*&@(@$^^*(,spdy/3",
                    CodecProtocol::SPDY_3, "spdy/3");
}

// Attempt to upgrade on second txn
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_txn_2) {
  this->getCodec().setAllowedUpgradeProtocols({"spdy/3"});
  auto handler1 = addSimpleStrictHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&handler1] {
      handler1->sendReplyWithBody(200, 100);
    });
  handler1->expectDetachTransaction();
  sendRequest(getGetRequest());
  flushRequestsAndLoop();
  expectResponse();

  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders();
  handler2->expectEOM([&handler2] {
      handler2->sendReplyWithBody(200, 100);
    });
  handler2->expectDetachTransaction();

  sendRequest(getUpgradeRequest("spdy/3"));
  flushRequestsAndLoop();
  expectResponse();
  gracefulShutdown();
}

// Upgrade on POST
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_post) {
  this->getCodec().setAllowedUpgradeProtocols({"spdy/3"});
  auto handler = addSimpleStrictHandler();
  handler->expectHeaders();
  handler->expectBody();
  EXPECT_CALL(mockController_, onSessionCodecChange(httpSession_));
  handler->expectEOM([&handler] {
      handler->sendReplyWithBody(200, 100);
    });
  handler->expectDetachTransaction();

  HTTPMessage req = getUpgradeRequest("spdy/3", HTTPMethod::POST, 10);
  auto streamID = sendRequest(req, false);
  clientCodec_->generateBody(requests_, streamID, makeBuf(10),
                             boost::none, true);
  // cheat and not sending EOM, it's a no-op
  flushRequestsAndLoop();
  expect101(CodecProtocol::SPDY_3, "spdy/3");
  expectResponse();
  gracefulShutdown();
}

// Upgrade on POST with a reply that comes before EOM, don't switch protocols
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_post_early_resp) {
  this->getCodec().setAllowedUpgradeProtocols({"spdy/3"});
  auto handler = addSimpleStrictHandler();
  handler->expectHeaders([&handler] {
      handler->sendReplyWithBody(200, 100);
    });
  handler->expectBody();
  handler->expectEOM();
  handler->expectDetachTransaction();

  HTTPMessage req = getUpgradeRequest("spdy/3", HTTPMethod::POST, 10);
  auto streamID = sendRequest(req, false);
  clientCodec_->generateBody(requests_, streamID, makeBuf(10),
                             boost::none, true);
  flushRequestsAndLoop();
  expectResponse();
  gracefulShutdown();
}

// Upgrade but with a pipelined HTTP request.  It is parsed as SPDY and
// rejected
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_extra) {
  this->getCodec().setAllowedUpgradeProtocols({"spdy/3"});
  auto handler = addSimpleStrictHandler();
  handler->expectHeaders();
  EXPECT_CALL(mockController_, onSessionCodecChange(httpSession_));
  handler->expectEOM([&handler] {
      handler->sendReplyWithBody(200, 100);
    });
  handler->expectDetachTransaction();

  sendRequest(getUpgradeRequest("spdy/3"));
  // It's a fatal to send this out on the HTTP1xCodec, so hack it manually
  transport_->addReadEvent("GET / HTTP/1.1\r\n"
                           "Upgrade: spdy/3\r\n"
                           "\r\n");
  flushRequestsAndLoop();
  expect101(CodecProtocol::SPDY_3, "spdy/3");
  expectResponse(200, ErrorCode::_SPDY_INVALID_STREAM);
  gracefulShutdown();
}

// Upgrade on POST with Expect: 100-Continue.  If the 100 goes out
// before the EOM is parsed, the 100 will be in HTTP.  This should be the normal
// case since the client *should* wait a bit for the 100 continue to come back
// before sending the POST.  But if the 101 is delayed beyond EOM, the 101
// will come via SPDY.
TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_post_100) {
  this->getCodec().setAllowedUpgradeProtocols({"spdy/3"});
  auto handler = addSimpleStrictHandler();
  handler->expectHeaders([&handler] {
      handler->sendHeaders(100, 0);
    });
  handler->expectBody();
  EXPECT_CALL(mockController_, onSessionCodecChange(httpSession_));
  handler->expectEOM([&handler] {
      handler->sendReplyWithBody(200, 100);
    });
  handler->expectDetachTransaction();

  HTTPMessage req = getUpgradeRequest("spdy/3", HTTPMethod::POST, 10);
  req.getHeaders().add(HTTP_HEADER_EXPECT, "100-continue");
  auto streamID = sendRequest(req, false);
  clientCodec_->generateBody(requests_, streamID, makeBuf(10),
                             boost::none, true);
  flushRequestsAndLoop();
  expect101(CodecProtocol::SPDY_3, "spdy/3", true /* expect 100 continue */);
  expectResponse();
  gracefulShutdown();
}

TEST_F(HTTPDownstreamSessionTest, http_upgrade_native_post_100_late) {
  this->getCodec().setAllowedUpgradeProtocols({"spdy/3"});
  auto handler = addSimpleStrictHandler();
  handler->expectHeaders();
  handler->expectBody();
  EXPECT_CALL(mockController_, onSessionCodecChange(httpSession_));
  handler->expectEOM([&handler] {
      handler->sendHeaders(100, 0);
      handler->sendReplyWithBody(200, 100);
    });
  handler->expectDetachTransaction();

  HTTPMessage req = getUpgradeRequest("spdy/3", HTTPMethod::POST, 10);
  req.getHeaders().add(HTTP_HEADER_EXPECT, "100-continue");
  auto streamID = sendRequest(req, false);
  clientCodec_->generateBody(requests_, streamID, makeBuf(10),
                             boost::none, true);
  flushRequestsAndLoop();
  expect101(CodecProtocol::SPDY_3, "spdy/3");
  expectResponse(200, ErrorCode::NO_ERROR, true /* expect 100 via SPDY */);
  gracefulShutdown();
}


TEST_F(SPDY3DownstreamSessionTest, spdy_prio) {
  testPriorities(8);

  cleanup();
}

// Test sending a GOAWAY while the downstream session is still processing
// the request that was an upgrade.  The reply GOAWAY should have last good
// stream = 1, not 0.
TEST_F(HTTPDownstreamSessionTest, http_upgrade_goaway_drain) {
  this->getCodec().setAllowedUpgradeProtocols({"h2c"});
  auto handler = addSimpleStrictHandler();
  handler->expectHeaders();
  handler->expectBody();
  EXPECT_CALL(mockController_, onSessionCodecChange(httpSession_));
  handler->expectEOM();
  handler->expectGoaway();
  handler->expectDetachTransaction();

  HTTPMessage req = getUpgradeRequest("h2c", HTTPMethod::POST, 10);
  HTTP2Codec::requestUpgrade(req);
  auto streamID = sendRequest(req, false);
  clientCodec_->generateBody(requests_, streamID, makeBuf(10),
                             boost::none, true);
  // cheat and not sending EOM, it's a no-op

  flushRequestsAndLoop();
  expect101(CodecProtocol::HTTP_2, "h2c");
  clientCodec_->generateConnectionPreface(requests_);
  clientCodec_->generateGoaway(requests_, 0, ErrorCode::NO_ERROR);
  flushRequestsAndLoop();
  eventBase_.runInLoop([&handler] {
      handler->sendReplyWithBody(200, 100);
    });
  HTTPSession::DestructorGuard g(httpSession_);
  eventBase_.loop();
  expectResponse(200, ErrorCode::NO_ERROR, false, true);
  expectDetachSession();
}

template <class C>
void HTTPDownstreamTest<C>::testPriorities(uint32_t numPriorities) {
  uint32_t iterations = 10;
  uint32_t maxPriority = numPriorities - 1;
  std::vector<std::unique_ptr<testing::NiceMock<MockHTTPHandler>>> handlers;
  for (int pri = numPriorities - 1; pri >= 0; pri--) {
    for (uint32_t i = 0; i < iterations; i++) {
      sendRequest("/", pri * (8 / numPriorities));
      InSequence handlerSequence;
      auto handler = addSimpleNiceHandler();
      auto rawHandler = handler.get();
      handlers.push_back(std::move(handler));
      rawHandler->expectHeaders();
      rawHandler->expectEOM([rawHandler] {
          rawHandler->sendReplyWithBody(200, 1000);
        });
      rawHandler->expectDetachTransaction([] {  });
    }
  }

  auto buf = requests_.move();
  buf->coalesce();
  requests_.append(std::move(buf));

  flushRequestsAndLoop();

  std::list<HTTPCodec::StreamID> streams;
  EXPECT_CALL(callbacks_, onMessageBegin(_, _))
    .Times(iterations * numPriorities);
  EXPECT_CALL(callbacks_, onHeadersComplete(_, _))
    .Times(iterations * numPriorities);
  // body is variable and hence ignored
  EXPECT_CALL(callbacks_, onMessageComplete(_, _))
    .Times(iterations * numPriorities)
    .WillRepeatedly(Invoke([&] (HTTPCodec::StreamID stream, bool upgrade) {
          streams.push_back(stream);
        }));

  parseOutput(*clientCodec_);

  // transactions finish in priority order (higher streamIDs first)
  EXPECT_EQ(streams.size(), iterations * numPriorities);
  auto txn = streams.begin();
  for (int band = maxPriority; band >= 0; band--) {
    auto upperID = iterations * 2 * (band + 1);
    auto lowerID = iterations * 2 * band;
    for (uint32_t i = 0; i < iterations; i++) {
      EXPECT_LE(lowerID, (uint32_t)*txn);
      EXPECT_GE(upperID, (uint32_t)*txn);
      ++txn;
    }
  }
}

// Verifies that the read timeout is not running when no ingress is expected/
// required to proceed
TEST_F(SPDY3DownstreamSessionTest, spdy_timeout) {
  sendRequest();
  sendRequest();

  httpSession_->setWriteBufferLimit(512);

  InSequence handlerSequence;
  auto handler1 = addSimpleStrictHandler();
  handler1->expectHeaders([this] { transport_->pauseWrites(); });
  handler1->expectEOM([&] {
      handler1->sendHeaders(200, 1000);
      handler1->sendBody(1000);
    });
  handler1->expectEgressPaused();
  auto handler2 = addSimpleStrictHandler();
  // handler2 is paused before it gets headers
  handler2->expectEgressPaused();
  handler2->expectHeaders();
  handler2->expectEOM([this] {
      // This transaction should start egress paused.  We've received the
      // EOM, so the timeout shouldn't be running delay 400ms and resume
      // writes, this keeps txn1 from getting a write timeout
      resumeWritesAfterDelay(milliseconds(400));
    });
  handler1->expectEgressResumed([&handler1] { handler1->txn_->sendEOM(); });
  handler2->expectEgressResumed([&handler2, this] {
      // delay an additional 200ms.  The total 600ms delay shouldn't fire
      // onTimeout
      eventBase_.tryRunAfterDelay([&handler2] {
          handler2->sendReplyWithBody(200, 400); }, 200
        );
    });
  handler1->expectDetachTransaction();
  handler2->expectDetachTransaction();

  flushRequestsAndLoop(false, milliseconds(0), milliseconds(10));

  cleanup();
}

// Verifies that the read timer is running while a transaction is blocked
// on a window update
TEST_F(SPDY3DownstreamSessionTest, spdy_timeout_win) {
  clientCodec_->getEgressSettings()->setSetting(SettingsId::INITIAL_WINDOW_SIZE,
                                                500);
  clientCodec_->generateSettings(requests_);
  auto streamID = sendRequest();

  InSequence handlerSequence;
  auto handler = addSimpleStrictHandler();
  handler->expectHeaders();
  handler->expectEOM([&] {
      handler->sendReplyWithBody(200, 1000);
    });
  handler->expectEgressPaused();
  handler->expectError([&] (const HTTPException& ex) {
      ASSERT_EQ(ex.getProxygenError(), kErrorWriteTimeout);
      ASSERT_EQ(
        folly::to<std::string>("ingress timeout, streamID=", streamID),
        std::string(ex.what()));
      handler->terminate();
    });
  handler->expectDetachTransaction();

  flushRequestsAndLoop();

  cleanup();
}

TYPED_TEST_CASE_P(HTTPDownstreamTest);

TYPED_TEST_P(HTTPDownstreamTest, testWritesDraining) {
  auto badCodec =
    makeServerCodec<typename TypeParam::Codec>(TypeParam::version);
  this->sendRequest();
  badCodec->generateHeader(this->requests_, 2 /* bad */, getGetRequest(), 1);

  this->expectDetachSession();

  InSequence handlerSequence;
  auto handler1 = this->addSimpleNiceHandler();
  handler1->expectHeaders();
  handler1->expectEOM();
  handler1->expectError([&] (const HTTPException& ex) {
      ASSERT_EQ(ex.getProxygenError(), kErrorEOF);
      ASSERT_EQ("Shutdown transport: EOF", std::string(ex.what()));
    });
  handler1->expectDetachTransaction();

  this->flushRequestsAndLoop();
}

TYPED_TEST_P(HTTPDownstreamTest, testBodySizeLimit) {
  this->clientCodec_->generateWindowUpdate(this->requests_, 0, 65536);
  this->sendRequest();
  this->sendRequest();

  InSequence handlerSequence;
  auto handler1 = this->addSimpleNiceHandler();
  handler1->expectHeaders();
  handler1->expectEOM();
  auto handler2 = this->addSimpleNiceHandler();
  handler2->expectHeaders();
  handler2->expectEOM([&] {
      handler1->sendReplyWithBody(200, 33000);
      handler2->sendReplyWithBody(200, 33000);
    });
  handler1->expectDetachTransaction();
  handler2->expectDetachTransaction();

  this->flushRequestsAndLoop();

  std::list<HTTPCodec::StreamID> streams;
  EXPECT_CALL(this->callbacks_, onMessageBegin(1, _));
  EXPECT_CALL(this->callbacks_, onHeadersComplete(1, _));
  EXPECT_CALL(this->callbacks_, onMessageBegin(3, _));
  EXPECT_CALL(this->callbacks_, onHeadersComplete(3, _));
  for (uint32_t i = 0; i < 8; i++) {
    EXPECT_CALL(this->callbacks_, onBody(1, _, _));
    EXPECT_CALL(this->callbacks_, onBody(3, _, _));
  }
  EXPECT_CALL(this->callbacks_, onBody(1, _, _));
  EXPECT_CALL(this->callbacks_, onMessageComplete(1, _));
  EXPECT_CALL(this->callbacks_, onBody(3, _, _));
  EXPECT_CALL(this->callbacks_, onMessageComplete(3, _));

  this->parseOutput(*this->clientCodec_);

  this->cleanup();
}

#define IF_HTTP2(X) \
  if (this->clientCodec_->getProtocol() == CodecProtocol::HTTP_2) { X; }

TYPED_TEST_P(HTTPDownstreamTest, testUniformPauseState) {
  this->httpSession_->setWriteBufferLimit(12000);
  this->clientCodec_->getEgressSettings()->setSetting(
    SettingsId::INITIAL_WINDOW_SIZE, 1000000);
  this->clientCodec_->generateSettings(this->requests_);
  this->clientCodec_->generateWindowUpdate(this->requests_, 0, 1000000);
  this->sendRequest("/", 1);
  this->sendRequest("/", 1);
  this->sendRequest("/", 2);

  InSequence handlerSequence;
  auto handler1 = this->addSimpleStrictHandler();
  handler1->expectHeaders();
  handler1->expectEOM();
  auto handler2 = this->addSimpleStrictHandler();
  handler2->expectHeaders();
  handler2->expectEOM([&] {
      handler1->sendHeaders(200, 24000);
      // triggers pause of all txns
      this->transport_->pauseWrites();
      handler1->txn_->sendBody(std::move(makeBuf(12000)));
      this->resumeWritesAfterDelay(milliseconds(50));
    });
  handler1->expectEgressPaused();
  handler2->expectEgressPaused();
  auto handler3 = this->addSimpleStrictHandler();
  handler3->expectEgressPaused();
  handler3->expectHeaders();
  handler3->expectEOM();

  handler1->expectEgressResumed([&] {
      // resume does not trigger another pause,
      handler1->txn_->sendBody(std::move(makeBuf(12000)));
    });
  // handler2 gets a fair shot, handler3 is not resumed
  // HTTP/2 priority is not implemented, so handler3 is like another 0 pri txn
  handler2->expectEgressResumed();
  IF_HTTP2(handler3->expectEgressResumed());
  handler1->expectEgressPaused();
  handler2->expectEgressPaused();
  IF_HTTP2(handler3->expectEgressPaused());

  handler1->expectEgressResumed();
  handler2->expectEgressResumed([&] {
      handler2->sendHeaders(200, 12000);
      handler2->txn_->sendBody(std::move(makeBuf(12000)));
      this->transport_->pauseWrites();
      this->resumeWritesAfterDelay(milliseconds(50));
    });
  // handler3 not resumed
  IF_HTTP2(handler3->expectEgressResumed());

  handler1->expectEgressPaused();
  handler2->expectEgressPaused();
  IF_HTTP2(handler3->expectEgressPaused());

  handler1->expectEgressResumed();
  handler2->expectEgressResumed([&] {
      handler1->txn_->sendEOM();
      handler2->txn_->sendEOM();
    });
  handler3->expectEgressResumed([&] {
      handler3->txn_->sendAbort();
    });

  handler3->expectDetachTransaction();
  handler1->expectDetachTransaction();
  handler2->expectDetachTransaction();

  this->flushRequestsAndLoop();

  this->cleanup();
}

// Test exceeding the MAX_CONCURRENT_STREAMS setting.  The txn should get
// REFUSED_STREAM, and other streams can complete normally
TYPED_TEST_P(HTTPDownstreamTest, testMaxTxns) {
  auto settings = this->httpSession_->getCodec().getEgressSettings();
  auto maxTxns = settings->getSetting(SettingsId::MAX_CONCURRENT_STREAMS,
                                      100);
  std::list<unique_ptr<StrictMock<MockHTTPHandler>>> handlers;
  {
    InSequence enforceOrder;
    for (auto i = 0U; i < maxTxns; i++) {
      this->sendRequest();
      auto handler = this->addSimpleStrictHandler();
      handler->expectHeaders();
      handler->expectEOM();
      handlers.push_back(std::move(handler));
    }
    auto streamID = this->sendRequest();
    this->clientCodec_->generateGoaway(this->requests_, 0, ErrorCode::NO_ERROR);

    for (auto& handler: handlers) {
      EXPECT_CALL(*handler, onGoaway(ErrorCode::NO_ERROR));
    }

    this->flushRequestsAndLoop();

    EXPECT_CALL(this->callbacks_, onSettings(_));
    EXPECT_CALL(this->callbacks_, onAbort(streamID, ErrorCode::REFUSED_STREAM));

    this->parseOutput(*this->clientCodec_);
  }
  // handlers can finish out of order?
  for (auto& handler: handlers) {
    handler->sendReplyWithBody(200, 100);
    handler->expectDetachTransaction();
  }
  this->expectDetachSession();
  this->eventBase_.loop();
}

// Set max streams=1
// send two spdy requests a few ms apart.
// Block writes
// generate a complete response for txn=1 before parsing txn=3
// HTTPSession should allow the txn=3 to be served rather than refusing it
TEST_F(SPDY3DownstreamSessionTest, spdy_max_concurrent_streams) {
  HTTPMessage req = getGetRequest();
  req.setHTTPVersion(1, 0);
  req.setWantsKeepalive(false);
  sendRequest(req);
  auto req2p = sendRequestLater(req, true);

  httpSession_->getCodecFilterChain()->getEgressSettings()->setSetting(
    SettingsId::MAX_CONCURRENT_STREAMS, 1);

  InSequence handlerSequence;
  auto handler1 = addSimpleStrictHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&handler1, req, this, &req2p] {
      transport_->pauseWrites();
      handler1->sendReplyWithBody(200, 100);
      req2p.setValue();
    });
  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders();
  handler2->expectEOM([&handler2, this] {
      handler2->sendReplyWithBody(200, 100);
      resumeWritesInLoop();
    });
  handler1->expectDetachTransaction();
  handler2->expectDetachTransaction();

  expectDetachSession();

  flushRequestsAndLoop();
}

REGISTER_TYPED_TEST_CASE_P(HTTPDownstreamTest,
                           testWritesDraining, testBodySizeLimit,
                           testUniformPauseState, testMaxTxns);

typedef ::testing::Types<SPDY3CodecPair, SPDY3_1CodecPair,
                         HTTP2CodecPair> ParallelCodecs;
INSTANTIATE_TYPED_TEST_CASE_P(ParallelCodecs,
                              HTTPDownstreamTest,
                              ParallelCodecs);

class SPDY31DownstreamTest : public HTTPDownstreamTest<SPDY3_1CodecPair> {
 public:
  SPDY31DownstreamTest()
      : HTTPDownstreamTest<SPDY3_1CodecPair>({-1, -1,
            2 * spdy::kInitialWindow}) {}
};

TEST_F(SPDY31DownstreamTest, testSessionFlowControl) {
  eventBase_.loopOnce();

  InSequence sequence;
  EXPECT_CALL(callbacks_, onSettings(_));
  EXPECT_CALL(callbacks_, onWindowUpdate(0, spdy::kInitialWindow));
  parseOutput(*clientCodec_);

  cleanup();
}

TEST_F(SPDY3DownstreamSessionTest, testEOFOnBlockedStream) {
  sendRequest();

  auto handler1 = addSimpleStrictHandler();

  InSequence handlerSequence;
  handler1->expectHeaders();
  handler1->expectEOM([&handler1, this] {
      handler1->sendReplyWithBody(200, 80000);
    });
  handler1->expectEgressPaused();

  handler1->expectError([&] (const HTTPException& ex) {
      // Not optimal to have a different error code here than the session
      // flow control case, but HTTPException direction is immutable and
      // building another one seems not future proof.
      EXPECT_EQ(ex.getDirection(), HTTPException::Direction::INGRESS);
    });
  handler1->expectDetachTransaction();

  expectDetachSession();

  flushRequestsAndLoop(true, milliseconds(10));
}

TEST_F(SPDY31DownstreamTest, testEOFOnBlockedSession) {
  sendRequest();
  sendRequest();

  InSequence handlerSequence;
  auto handler1 = addSimpleStrictHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&handler1, this] {
      handler1->sendHeaders(200, 40000);
      handler1->sendBody(32768);
    });
  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders();
  handler2->expectEOM([&handler2, this] {
      handler2->sendHeaders(200, 40000);
      handler2->sendBody(32768);
      eventBase_.runInLoop([this] { transport_->addReadEOF(milliseconds(0)); });
    });

  handler1->expectEgressPaused();
  handler2->expectEgressPaused();
  handler1->expectEgressResumed();
  handler2->expectEgressResumed();
  handler1->expectError([&] (const HTTPException& ex) {
      EXPECT_EQ(ex.getDirection(),
                HTTPException::Direction::INGRESS_AND_EGRESS);
    });
  handler1->expectDetachTransaction();
  handler2->expectError([&] (const HTTPException& ex) {
      EXPECT_EQ(ex.getDirection(),
                HTTPException::Direction::INGRESS_AND_EGRESS);
    });
  handler2->expectDetachTransaction();

  expectDetachSession();

  flushRequestsAndLoop();
}


TEST_F(SPDY3DownstreamSessionTest, new_txn_egress_paused) {
  // Send 1 request with prio=0
  // Have egress pause while sending the first response
  // Send a second request with prio=1
  //   -- the new txn should start egress paused
  // Finish the body and eom both responses
  // Unpause egress
  // The first txn should complete first

  sendRequest("/", 0);
  auto req2 = getGetRequest();
  req2.setPriority(1);
  auto req2p = sendRequestLater(req2, true);

  unique_ptr<StrictMock<MockHTTPHandler>> handler1;
  unique_ptr<StrictMock<MockHTTPHandler>> handler2;

  httpSession_->setWriteBufferLimit(200); // lower the per session buffer limit
  {
    InSequence handlerSequence;
    handler1 = addSimpleStrictHandler();
    handler1->expectHeaders();
    handler1->expectEOM([&handler1, this, &req2p] {
        this->transport_->pauseWrites();
        handler1->sendHeaders(200, 1000);
        handler1->sendBody(100); // headers + 100 bytes - over the limit
        req2p.setValue();
      });
    handler1->expectEgressPaused([] { LOG(INFO) << "paused 1"; });

    handler2 = addSimpleStrictHandler();
    handler2->expectEgressPaused(); // starts paused
    handler2->expectHeaders();
    handler2->expectEOM([&] {
        // Technically shouldn't send while handler is egress paused, but meh.
        handler1->sendBody(900);
        handler1->txn_->sendEOM();
        handler2->sendReplyWithBody(200, 1000);
        resumeWritesInLoop();
      });
    handler1->expectDetachTransaction();
    handler2->expectDetachTransaction();
  }
  HTTPSession::DestructorGuard g(httpSession_);
  flushRequestsAndLoop();

  std::list<HTTPCodec::StreamID> streams;
  EXPECT_CALL(callbacks_, onMessageBegin(_, _))
    .Times(2);
  EXPECT_CALL(callbacks_, onHeadersComplete(_, _))
    .Times(2);
  // body is variable and hence ignored;
  EXPECT_CALL(callbacks_, onMessageComplete(_, _))
    .WillRepeatedly(Invoke([&] (HTTPCodec::StreamID stream, bool upgrade) {
          streams.push_back(stream);
        }));
  parseOutput(*clientCodec_);

  cleanup();
}

TEST_F(HTTP2DownstreamSessionTest, zero_delta_window_update) {
  // generateHeader() will create a session and a transaction
  auto streamID = sendHeader();
  // First generate a frame with delta=1 so as to pass the checks, and then
  // hack the frame so that delta=0 without modifying other checks
  clientCodec_->generateWindowUpdate(requests_, streamID, 1);
  requests_.trimEnd(http2::kFrameWindowUpdateSize);
  QueueAppender appender(&requests_, http2::kFrameWindowUpdateSize);
  appender.writeBE<uint32_t>(0);

  auto handler = addSimpleStrictHandler();

  InSequence handlerSequence;
  handler->expectHeaders();
  handler->expectError([&] (const HTTPException& ex) {
      ASSERT_EQ(ex.getCodecStatusCode(), ErrorCode::PROTOCOL_ERROR);
      ASSERT_EQ(
        "streamID=1 with HTTP2Codec stream error: window update delta=0",
        std::string(ex.what()));
    });
  handler->expectDetachTransaction();
  expectDetachSession();

  flushRequestsAndLoop();
}

TEST_F(HTTP2DownstreamSessionTest, padding_flow_control) {
  // generateHeader() will create a session and a transaction
  auto streamID = sendHeader();
  // This sends a total of 33kb including padding, so we should get a session
  // and stream window update
  for (auto i = 0; i < 129; i++) {
    clientCodec_->generateBody(requests_, streamID, makeBuf(1), 255, false);
  }

  auto handler = addSimpleStrictHandler();

  InSequence handlerSequence;
  handler->expectHeaders([&] {
      handler->txn_->pauseIngress();
      eventBase_.runAfterDelay([&] { handler->txn_->resumeIngress(); },
                               100);
    });
  EXPECT_CALL(*handler, onBody(_))
    .Times(129);
  handler->expectError();
  handler->expectDetachTransaction();

  HTTPSession::DestructorGuard g(httpSession_);
  flushRequestsAndLoop(false, milliseconds(0), milliseconds(0), [&] {
      clientCodec_->generateRstStream(requests_, streamID, ErrorCode::CANCEL);
      clientCodec_->generateGoaway(requests_, 0, ErrorCode::NO_ERROR);
      transport_->addReadEvent(requests_, milliseconds(110));
    });

  std::list<HTTPCodec::StreamID> streams;
  EXPECT_CALL(callbacks_, onWindowUpdate(0, _));
  EXPECT_CALL(callbacks_, onWindowUpdate(1, _));
  parseOutput(*clientCodec_);
  expectDetachSession();
}

TEST_F(HTTP2DownstreamSessionTest, graceful_drain_on_timeout) {
  InSequence handlerSequence;
  std::chrono::milliseconds gracefulTimeout(200);
  getCodec().enableDoubleGoawayDrain();
  EXPECT_CALL(mockController_, getGracefulShutdownTimeout())
    .WillOnce(InvokeWithoutArgs([&] {
          // Once session asks for graceful shutdown timeout, expect the client
          // to receive the first GOAWAY
          eventBase_.runInLoop([&] {
              EXPECT_CALL(callbacks_,
                          onGoaway(std::numeric_limits<int32_t>::max(),
                                   ErrorCode::NO_ERROR, _));
              parseOutput(*clientCodec_);
            });
          return gracefulTimeout;
        }));


  // Simulate ConnectionManager idle timeout
  eventBase_.runAfterDelay([&] { httpSession_->timeoutExpired(); },
                           transactionTimeouts_->getDefaultTimeout().count());
  HTTPSession::DestructorGuard g(httpSession_);
  auto start = getCurrentTime();
  eventBase_.loop();
  auto finish = getCurrentTime();
  auto minDuration =
    gracefulTimeout + transactionTimeouts_->getDefaultTimeout();
  EXPECT_GE((finish - start).count(), minDuration.count());
  EXPECT_CALL(callbacks_, onGoaway(0, ErrorCode::NO_ERROR, _));
  parseOutput(*clientCodec_);
  expectDetachSession();
}

/*
 * The sequence of streams are generated in the following order:
 * - [client --> server] request 1st stream (getGetRequest())
 * - [server --> client] respond 1st stream (res with length 100)
 * - [server --> client] request 2nd stream (req)
 * - [server --> client] respond 2nd stream (res with length 200 + EOM)
 * - [client --> server] RST_STREAM on the 1st stream
 */
TEST_F(HTTP2DownstreamSessionTest, server_push) {
  HTTP2Codec serverCodec(TransportDirection::DOWNSTREAM);
  HTTP2Codec clientCodec(TransportDirection::UPSTREAM);
  IOBufQueue output{IOBufQueue::cacheChainLength()};
  IOBufQueue input{IOBufQueue::cacheChainLength()};

  // Create a dummy request and a dummy response messages
  HTTPMessage req, res;
  req.getHeaders().set("HOST", "www.foo.com");
  req.setURL("https://www.foo.com/");
  res.setStatusCode(200);
  res.setStatusMessage("Ohai");

  // Construct data sent from client to server
  auto assocStreamId = HTTPCodec::StreamID(1);
  clientCodec.getEgressSettings()->setSetting(SettingsId::ENABLE_PUSH, 1);
  clientCodec.generateConnectionPreface(output);
  clientCodec.generateSettings(output);
  // generateHeader() will create a session and a transaction
  clientCodec.generateHeader(output, assocStreamId, getGetRequest(),
                             0, false, nullptr);

  auto handler = addSimpleStrictHandler();
  StrictMock<MockHTTPPushHandler> pushHandler;

  InSequence handlerSequence;
  handler->expectHeaders([&] {
      // Generate response for the associated stream
      handler->txn_->sendHeaders(res);
      handler->txn_->sendBody(makeBuf(100));
      handler->txn_->pauseIngress();

      auto* pushTxn = handler->txn_->newPushedTransaction(&pushHandler);
      ASSERT_NE(pushTxn, nullptr);
      // Generate a push request (PUSH_PROMISE)
      pushTxn->sendHeaders(req);
      // Generate a push response
      auto pri = handler->txn_->getPriority();
      res.setHTTP2Priority(std::make_tuple(pri.streamDependency,
                                           pri.exclusive, pri.weight));
      pushTxn->sendHeaders(res);
      pushTxn->sendBody(makeBuf(200));
      pushTxn->sendEOM();

      eventBase_.runAfterDelay([&] { handler->txn_->resumeIngress(); },
                               100);
    });
  EXPECT_CALL(pushHandler, setTransaction(_))
    .WillOnce(Invoke([&] (HTTPTransaction* txn) {
          pushHandler.txn_ = txn; }));
  EXPECT_CALL(pushHandler, detachTransaction());
  handler->expectError();
  handler->expectDetachTransaction();

  transport_->addReadEvent(output, milliseconds(0));
  clientCodec.generateRstStream(output, assocStreamId, ErrorCode::CANCEL);
  clientCodec.generateGoaway(output, 2, ErrorCode::NO_ERROR);
  transport_->addReadEvent(output, milliseconds(200));
  transport_->startReadEvents();
  HTTPSession::DestructorGuard g(httpSession_);
  eventBase_.loop();

  EXPECT_CALL(callbacks_, onMessageBegin(1, _));
  EXPECT_CALL(callbacks_, onHeadersComplete(1, _));
  EXPECT_CALL(callbacks_, onPushMessageBegin(2, 1, _));
  EXPECT_CALL(callbacks_, onHeadersComplete(2, _));
  EXPECT_CALL(callbacks_, onMessageBegin(2, _));
  EXPECT_CALL(callbacks_, onHeadersComplete(2, _));
  EXPECT_CALL(callbacks_, onMessageComplete(2, _));
  clientCodec.setCallback(&callbacks_);
  parseOutput(clientCodec);
  expectDetachSession();
}

TEST_F(HTTP2DownstreamSessionTest, server_push_abort_paused) {
  HTTP2Codec serverCodec(TransportDirection::DOWNSTREAM);
  HTTP2Codec clientCodec(TransportDirection::UPSTREAM);
  IOBufQueue output{IOBufQueue::cacheChainLength()};
  IOBufQueue input{IOBufQueue::cacheChainLength()};

  // Create a dummy request and a dummy response messages
  HTTPMessage req, res;
  req.getHeaders().set("HOST", "www.foo.com");
  req.setURL("https://www.foo.com/");
  res.setStatusCode(200);
  res.setStatusMessage("Ohai");

  // Construct data sent from client to server
  auto assocStreamId = HTTPCodec::StreamID(1);
  clientCodec.getEgressSettings()->setSetting(SettingsId::ENABLE_PUSH, 1);
  clientCodec.generateConnectionPreface(output);
  clientCodec.generateSettings(output);
  // generateHeader() will create a session and a transaction
  clientCodec.generateHeader(output, assocStreamId, getGetRequest(),
                             0, false, nullptr);

  auto handler = addSimpleStrictHandler();
  StrictMock<MockHTTPPushHandler> pushHandler;

  InSequence handlerSequence;
  handler->expectHeaders([&] {
      // Generate response for the associated stream
      this->transport_->pauseWrites();
      handler->txn_->sendHeaders(res);
      handler->txn_->sendBody(makeBuf(100));
      handler->txn_->pauseIngress();

      auto* pushTxn = handler->txn_->newPushedTransaction(&pushHandler);
      ASSERT_NE(pushTxn, nullptr);
      // Generate a push request (PUSH_PROMISE)
      pushTxn->sendHeaders(req);
    });
  EXPECT_CALL(pushHandler, setTransaction(_))
    .WillOnce(Invoke([&] (HTTPTransaction* txn) {
          pushHandler.txn_ = txn; }));
  EXPECT_CALL(pushHandler, onError(_));
  EXPECT_CALL(pushHandler, detachTransaction());
  handler->expectError();
  handler->expectDetachTransaction();

  transport_->addReadEvent(output, milliseconds(0));
  // Cancels everything
  clientCodec.generateRstStream(output, assocStreamId, ErrorCode::CANCEL);
  transport_->addReadEvent(output, milliseconds(10));
  transport_->startReadEvents();
  HTTPSession::DestructorGuard g(httpSession_);
  eventBase_.loop();

  clientCodec.setCallback(&callbacks_);
  parseOutput(clientCodec);
  expectDetachSession();
}

TEST_F(HTTP2DownstreamSessionTest, test_priority_weights_tiny_ratio) {
  // Create a transaction with egress and a ratio small enough that
  // ratio*4096 < 1.
  //
  //     root
  //     /  \                                                 level 1
  //   256   1 (no egress)
  //        / \                                               level 2
  //      256  1  <-- has ratio (1/257)^2
  InSequence enforceOrder;
  auto req1 = getGetRequest();
  auto req2 = getGetRequest();
  req1.setHTTP2Priority(HTTPMessage::HTTPPriority{0, false, 255});
  req2.setHTTP2Priority(HTTPMessage::HTTPPriority{0, false, 0});

  auto id1 = sendRequest(req1);
  auto id2 = sendRequest(req2);
  req1.setHTTP2Priority(HTTPMessage::HTTPPriority{id2, false, 255});
  req2.setHTTP2Priority(HTTPMessage::HTTPPriority{id2, false, 0});
  auto id3 = sendRequest(req1);
  auto id4 = sendRequest(req2);

  auto handler1 = addSimpleStrictHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&] {
      handler1->sendReplyWithBody(200, 4 * 1024);
    });
  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders();
  handler2->expectEOM();
  auto handler3 = addSimpleStrictHandler();
  handler3->expectHeaders();
  handler3->expectEOM([&] {
      handler3->sendReplyWithBody(200, 15);
    });
  auto handler4 = addSimpleStrictHandler();
  handler4->expectHeaders();
  handler4->expectEOM([&] {
      handler4->sendReplyWithBody(200, 1);
    });

  handler1->expectDetachTransaction();
  handler3->expectDetachTransaction();
  handler4->expectDetachTransaction([&] {
      handler2->txn_->sendAbort();
    });
  handler2->expectDetachTransaction();
  flushRequestsAndLoop();
  httpSession_->closeWhenIdle();
  expectDetachSession();
  eventBase_.loop();
}

TEST_F(HTTP2DownstreamSessionTest, test_disable_priorities) {
  // turn off HTTP2 priorities
  httpSession_->setHTTP2PrioritiesEnabled(false);

  InSequence enforceOrder;
  HTTPMessage req1 = getGetRequest();
  req1.setHTTP2Priority(HTTPMessage::HTTPPriority{0, false, 0});
  sendRequest(req1);

  HTTPMessage req2 = getGetRequest();
  req2.setHTTP2Priority(HTTPMessage::HTTPPriority{0, false, 255});
  sendRequest(req2);

  auto handler1 = addSimpleStrictHandler();
  handler1->expectHeaders();
  handler1->expectEOM([&] {
      handler1->sendReplyWithBody(200, 4 * 1024);
    });

  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders();
  handler2->expectEOM([&] {
      handler2->sendReplyWithBody(200, 4 * 1024);
    });

  // expecting handler 1 to finish first irrespective of
  // request 2 having higher weight
  handler1->expectDetachTransaction();
  handler2->expectDetachTransaction();

  flushRequestsAndLoop();
  httpSession_->closeWhenIdle();
  expectDetachSession();
  eventBase_.loop();
}

TEST_F(HTTP2DownstreamSessionTest, continuation_timeout) {
  // Split the headers at 15 bytes to force a CONTINUATION frame
  HTTP2Codec::setHeaderSplitSize(15);
  auto req = getGetRequest();
  req.getHeaders().add("x", "ZZZZZZZ");
  sendRequest(req);

  HTTPSession::DestructorGuard g(httpSession_);
  // Send the Connection Preface and HEADERS immediately
  auto buf = requests_.split(40);
  buf->coalesce();
  transport_->addReadEvent(buf->data(), buf->length(), milliseconds(0));
  // Delay the CONTINUATION by the read timeout.
  flushRequestsAndLoop(false, milliseconds(0),
                       transactionTimeouts_->getDefaultTimeout());

  EXPECT_CALL(callbacks_, onSettings(_));
  // We get 1 INTERNAL_ERROR on the timeout
  EXPECT_CALL(callbacks_, onAbort(1, ErrorCode::INTERNAL_ERROR));
  // And a STREAM_CLOSED when the in-flight CONTINUATION gets parsed.
  EXPECT_CALL(callbacks_, onAbort(1, ErrorCode::STREAM_CLOSED));
  parseOutput(*clientCodec_);
  cleanup();
}

TEST_F(HTTP2DownstreamSessionTest, test_priority_weights) {
  InSequence enforceOrder;
  // virtual priority node with pri=4
  auto priGroupID = clientCodec_->createStream();
  clientCodec_->generatePriority(
    requests_, priGroupID, HTTPMessage::HTTPPriority(0, false, 3));
  // Both txn's are at equal pri=16
  auto id1 = sendRequest();
  auto id2 = sendRequest();

  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders();
  handler1->expectEOM([&] {
      handler1->sendHeaders(200, 12 * 1024);
      handler1->txn_->sendBody(makeBuf(4 * 1024));
    });
  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders();
  handler2->expectEOM([&] {
      handler2->sendHeaders(200, 12 * 1024);
      handler2->txn_->sendBody(makeBuf(4 * 1024));
    });

  // twice- once to send and once to receive
  flushRequestsAndLoopN(2);
  EXPECT_CALL(callbacks_, onSettings(_));
  EXPECT_CALL(callbacks_, onMessageBegin(id1, _));
  EXPECT_CALL(callbacks_, onHeadersComplete(id1, _));
  EXPECT_CALL(callbacks_, onMessageBegin(id2, _));
  EXPECT_CALL(callbacks_, onHeadersComplete(id2, _));
  EXPECT_CALL(callbacks_, onBody(id1, _, _))
    .WillOnce(ExpectBodyLen(4 * 1024));
  EXPECT_CALL(callbacks_, onBody(id2, _, _))
    .WillOnce(ExpectBodyLen(4 * 1024));
  parseOutput(*clientCodec_);

  // update handler2 to be in the pri-group (which has lower weight)
  clientCodec_->generatePriority(
    requests_, id2, HTTPMessage::HTTPPriority(priGroupID, false, 15));

  eventBase_.runInLoop([&] {
      handler1->txn_->sendBody(makeBuf(4 * 1024));
      handler2->txn_->sendBody(makeBuf(4 * 1024));
    });
  flushRequestsAndLoopN(2);

  EXPECT_CALL(callbacks_, onBody(id1, _, _))
    .WillOnce(ExpectBodyLen(4 * 1024));
  EXPECT_CALL(callbacks_, onBody(id2, _, _))
    .WillOnce(ExpectBodyLen(1 * 1024))
    .WillOnce(ExpectBodyLen(3 * 1024));
  parseOutput(*clientCodec_);

  // update vnode weight to match txn1 weight
  clientCodec_->generatePriority(requests_, priGroupID,
                                 HTTPMessage::HTTPPriority(0, false, 15));
  eventBase_.runInLoop([&] {
      handler1->txn_->sendBody(makeBuf(4 * 1024));
      handler1->txn_->sendEOM();
      handler2->txn_->sendBody(makeBuf(4 * 1024));
      handler2->txn_->sendEOM();
    });
  handler1->expectDetachTransaction();
  handler2->expectDetachTransaction();
  flushRequestsAndLoopN(2);

  // expect 32/32
  EXPECT_CALL(callbacks_, onBody(id1, _, _))
    .WillOnce(ExpectBodyLen(4 * 1024));
  EXPECT_CALL(callbacks_, onMessageComplete(id1, _));
  EXPECT_CALL(callbacks_, onBody(id2, _, _))
    .WillOnce(ExpectBodyLen(4 * 1024));
  EXPECT_CALL(callbacks_, onMessageComplete(id2, _));
  parseOutput(*clientCodec_);

  httpSession_->closeWhenIdle();
  expectDetachSession();
  this->eventBase_.loop();
}

TEST_F(HTTP2DownstreamSessionTest, test_priority_weights_tiny_window) {
  httpSession_->setWriteBufferLimit(2 * 65536);
  InSequence enforceOrder;
  auto id1 = sendRequest();
  auto id2 = sendRequest();

  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders();
  handler1->expectEOM([&] {
      handler1->sendReplyWithBody(200, 32 * 1024);
    });
  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders();
  handler2->expectEOM([&] {
      handler2->sendReplyWithBody(200, 32 * 1024);
    });

  handler1->expectDetachTransaction();

  // twice- once to send and once to receive
  flushRequestsAndLoopN(2);
  EXPECT_CALL(callbacks_, onSettings(_));
  EXPECT_CALL(callbacks_, onMessageBegin(id1, _));
  EXPECT_CALL(callbacks_, onHeadersComplete(id1, _));
  EXPECT_CALL(callbacks_, onMessageBegin(id2, _));
  EXPECT_CALL(callbacks_, onHeadersComplete(id2, _));
  for (auto i = 0; i < 7; i++) {
    EXPECT_CALL(callbacks_, onBody(id1, _, _))
      .WillOnce(ExpectBodyLen(4 * 1024));
    EXPECT_CALL(callbacks_, onBody(id2, _, _))
      .WillOnce(ExpectBodyLen(4 * 1024));
  }
  EXPECT_CALL(callbacks_, onBody(id1, _, _))
    .WillOnce(ExpectBodyLen(4 * 1024 - 1));
  EXPECT_CALL(callbacks_, onBody(id2, _, _))
    .WillOnce(ExpectBodyLen(4 * 1024 - 1));
  EXPECT_CALL(callbacks_, onBody(id1, _, _))
    .WillOnce(ExpectBodyLen(1));
  EXPECT_CALL(callbacks_, onMessageComplete(id1, _));
  parseOutput(*clientCodec_);

  // open the window
  clientCodec_->generateWindowUpdate(requests_, 0, 100);
  handler2->expectDetachTransaction();
  flushRequestsAndLoopN(2);

  EXPECT_CALL(callbacks_, onBody(id2, _, _))
    .WillOnce(ExpectBodyLen(1));
  EXPECT_CALL(callbacks_, onMessageComplete(id2, _));
  parseOutput(*clientCodec_);

  httpSession_->closeWhenIdle();
  expectDetachSession();
  this->eventBase_.loop();
}

TEST_F(HTTP2DownstreamSessionTest, test_short_content_length) {
  InSequence enforceOrder;
  auto req = getPostRequest(10);
  auto streamID = sendRequest(req, false);
  clientCodec_->generateBody(requests_, streamID, makeBuf(20),
                             boost::none, true);
  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders();
  handler1->expectError([&handler1] (const HTTPException& ex) {
      EXPECT_EQ(ex.getProxygenError(), kErrorParseBody);
      handler1->txn_->sendAbort();
    });
  handler1->expectDetachTransaction();
  flushRequestsAndLoop();

  gracefulShutdown();
}

/**
 * If handler chooses to untie itself with transaction during onError,
 * detachTransaction shouldn't be expected
 */
TEST_F(HTTP2DownstreamSessionTest, test_bad_content_length_untie_handler) {
  InSequence enforceOrder;
  auto req = getPostRequest(10);
  auto streamID = sendRequest(req, false);
  clientCodec_->generateBody(
      requests_,
      streamID,
      makeBuf(20),
      boost::none,
      true);
  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders();
  handler1->expectError([&] (const HTTPException&) {
      if (handler1->txn_) {
        handler1->txn_->setHandler(nullptr);
      }
      handler1->txn_ = nullptr;
    });
  flushRequestsAndLoop();

  gracefulShutdown();
}

TEST_F(HTTP2DownstreamSessionTest, test_long_content_length) {
  InSequence enforceOrder;
  auto req = getPostRequest(30);
  auto streamID = sendRequest(req, false);
  clientCodec_->generateBody(requests_, streamID, makeBuf(20),
                             boost::none, true);
  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders();
  handler1->expectBody();
  handler1->expectError([&handler1] (const HTTPException& ex) {
      EXPECT_EQ(ex.getProxygenError(), kErrorParseBody);
      handler1->txn_->sendAbort();
    });
  handler1->expectDetachTransaction();
  flushRequestsAndLoop();

  gracefulShutdown();
}

TEST_F(HTTP2DownstreamSessionTest, test_malformed_content_length) {
  InSequence enforceOrder;
  auto req = getPostRequest();
  req.getHeaders().set(HTTP_HEADER_CONTENT_LENGTH, "malformed");
  auto streamID = sendRequest(req, false);
  clientCodec_->generateBody(requests_, streamID, makeBuf(20),
                             boost::none, true);
  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders();
  handler1->expectBody();
  handler1->expectEOM([&handler1] {
      handler1->sendReplyWithBody(200, 100);
    });
  handler1->expectDetachTransaction();
  flushRequestsAndLoop();

  gracefulShutdown();
}

TEST_F(HTTP2DownstreamSessionTest, test_head_content_length) {
  InSequence enforceOrder;
  auto req = getGetRequest();
  req.setMethod(HTTPMethod::HEAD);
  auto streamID = sendRequest(req);
  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders();
  handler1->expectEOM([&handler1] {
      handler1->sendHeaders(200, 100);
      // no body for head
      handler1->txn_->sendEOM();
    });
  handler1->expectDetachTransaction();
  flushRequestsAndLoop();

  gracefulShutdown();
}

TEST_F(HTTP2DownstreamSessionTest, test_304_content_length) {
  InSequence enforceOrder;
  auto req = getGetRequest();
  req.setMethod(HTTPMethod::HEAD);
  auto streamID = sendRequest(req);
  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders();
  handler1->expectEOM([&handler1] {
      handler1->sendHeaders(304, 100);
      handler1->txn_->sendEOM();
    });
  handler1->expectDetachTransaction();
  flushRequestsAndLoop();

  gracefulShutdown();
}

// chunked with wrong content-length
TEST_F(HTTPDownstreamSessionTest, http_short_content_length) {
  InSequence enforceOrder;
  auto req = getPostRequest(10);
  req.setIsChunked(true);
  req.getHeaders().add(HTTP_HEADER_TRANSFER_ENCODING, "chunked");
  auto streamID = sendRequest(req, false);
  clientCodec_->generateChunkHeader(requests_, streamID, 20);
  clientCodec_->generateBody(requests_, streamID, makeBuf(20), boost::none,
                             false);
  clientCodec_->generateChunkTerminator(requests_, streamID);
  clientCodec_->generateEOM(requests_, streamID);
  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders();
  EXPECT_CALL(*handler1, onChunkHeader(20));

  handler1->expectError([&handler1] (const HTTPException& ex) {
      EXPECT_EQ(ex.getProxygenError(), kErrorParseBody);
      handler1->txn_->sendAbort();
    });
  handler1->expectDetachTransaction();
  expectDetachSession();
  flushRequestsAndLoop();

}

TEST_F(HTTP2DownstreamSessionTest, test_session_stall_by_flow_control) {
  NiceMock<MockHTTPSessionStats> stats;
  // By default the send and receive windows are 64K each.
  // If we use only a single transaction, that transaction
  // will be paused on reaching 64K. Therefore, to pause the session,
  // it is used 2 transactions each sending 32K.

  // Make write buffer limit exceding 64K, for example 128K
  httpSession_->setWriteBufferLimit(128 * 1024);
  httpSession_->setSessionStats(&stats);

  InSequence enforceOrder;
  sendRequest();
  sendRequest();

  auto handler1 = addSimpleStrictHandler();

  handler1->expectHeaders();
  handler1->expectEOM([&] {
      handler1->sendReplyWithBody(200, 32 * 1024);
    });

  auto handler2 = addSimpleStrictHandler();
  handler2->expectHeaders();
  handler2->expectEOM([&] {
      handler2->sendReplyWithBody(200, 32 * 1024);
    });

  EXPECT_CALL(stats, recordSessionStalled()).Times(1);

  handler1->expectDetachTransaction();

  // twice- once to send and once to receive
  flushRequestsAndLoopN(2);

  // open the window
  clientCodec_->generateWindowUpdate(requests_, 0, 100);
  handler2->expectDetachTransaction();
  flushRequestsAndLoopN(2);

  httpSession_->closeWhenIdle();
  expectDetachSession();
  flushRequestsAndLoop();
}

TEST_F(HTTP2DownstreamSessionTest, test_transaction_stall_by_flow_control) {
  StrictMock<MockHTTPSessionStats> stats;

  httpSession_->setSessionStats(&stats);

  // Set the client side stream level flow control wind to 500 bytes,
  // and try to send 1000 bytes through it.
  // Then the flow control kicks in and stalls the transaction.
  clientCodec_->getEgressSettings()->setSetting(SettingsId::INITIAL_WINDOW_SIZE,
                                                500);
  clientCodec_->generateSettings(requests_);

  auto streamID = sendRequest();

  EXPECT_CALL(stats, recordTransactionOpened());

  InSequence handlerSequence;
  auto handler = addSimpleStrictHandler();
  handler->expectHeaders();
  handler->expectEOM([&] {
      handler->sendReplyWithBody(200, 1000);
    });

  EXPECT_CALL(stats, recordTransactionStalled());
  handler->expectEgressPaused();

  handler->expectError([&] (const HTTPException& ex) {
      ASSERT_EQ(ex.getProxygenError(), kErrorWriteTimeout);
      ASSERT_EQ(
        folly::to<std::string>("ingress timeout, streamID=", streamID),
        std::string(ex.what()));
      handler->terminate();
    });

  handler->expectDetachTransaction();

  EXPECT_CALL(stats, recordTransactionClosed());

  flushRequestsAndLoop();
  gracefulShutdown();
}

TEST_F(HTTP2DownstreamSessionTest, test_transaction_not_stall_by_flow_control) {
  StrictMock<MockHTTPSessionStats> stats;

  httpSession_->setSessionStats(&stats);

  clientCodec_->getEgressSettings()->setSetting(SettingsId::INITIAL_WINDOW_SIZE,
                                                500);
  clientCodec_->generateSettings(requests_);

  sendRequest();

  EXPECT_CALL(stats, recordTransactionOpened());

  InSequence handlerSequence;
  auto handler = addSimpleStrictHandler();
  handler->expectHeaders();
  handler->expectEOM([&] {
      handler->sendReplyWithBody(200, 500);
    });

  // The egtress paused is notified due to existing logics,
  // but egress transaction should not be counted as stalled by flow control,
  // because there is nore more bytes to send
  handler->expectEgressPaused();

  handler->expectDetachTransaction();

  EXPECT_CALL(stats, recordTransactionClosed());

  flushRequestsAndLoop();
  gracefulShutdown();
}
