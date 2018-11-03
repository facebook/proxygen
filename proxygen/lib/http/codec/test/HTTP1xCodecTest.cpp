/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/test/MockHTTPCodec.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <proxygen/lib/utils/Base64.h>

using namespace proxygen;
using namespace std;
using namespace testing;

class HTTP1xCodecCallback : public HTTPCodec::Callback {
 public:
  HTTP1xCodecCallback() {}

  void onMessageBegin(HTTPCodec::StreamID /*stream*/,
                      HTTPMessage* /*msg*/) override {}
  void onPushMessageBegin(HTTPCodec::StreamID /*stream*/,
                          HTTPCodec::StreamID /*assocStream*/,
                          HTTPMessage* /*msg*/) override {}
  void onHeadersComplete(HTTPCodec::StreamID /*stream*/,
                         std::unique_ptr<HTTPMessage> msg) override {
    headersComplete++;
    headerSize = msg->getIngressHeaderSize();
    msg_ = std::move(msg);
  }
  void onBody(HTTPCodec::StreamID /*stream*/,
              std::unique_ptr<folly::IOBuf> chain,
              uint16_t /*padding*/) override {
    bodyLen += chain->computeChainDataLength();
  }
  void onChunkHeader(HTTPCodec::StreamID /*stream*/,
                     size_t /*length*/) override {}
  void onChunkComplete(HTTPCodec::StreamID /*stream*/) override {}
  void onTrailersComplete(HTTPCodec::StreamID /*stream*/,
                          std::unique_ptr<HTTPHeaders> /*trailers*/) override {}
  void onMessageComplete(HTTPCodec::StreamID /*stream*/,
                         bool /*upgrade*/) override {
    ++messageComplete;
  }
  void onError(HTTPCodec::StreamID /*stream*/,
               const HTTPException& /*error*/,
               bool /*newTxn*/) override {
    ++errors;
    LOG(ERROR) << "parse error";
  }

  uint32_t headersComplete{0};
  uint32_t messageComplete{0};
  uint32_t errors{0};
  uint32_t bodyLen{0};
  HTTPHeaderSize headerSize;
  std::unique_ptr<HTTPMessage> msg_;
};

unique_ptr<folly::IOBuf> getSimpleRequestData() {
  string req("GET /yeah HTTP/1.1\nHost: www.facebook.com\n\n");
  return folly::IOBuf::copyBuffer(req);
}

TEST(HTTP1xCodecTest, TestSimpleHeaders) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback callbacks;
  codec.setCallback(&callbacks);
  auto buffer = getSimpleRequestData();
  codec.onIngress(*buffer);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(buffer->length(), callbacks.headerSize.uncompressed);
  EXPECT_EQ(callbacks.headerSize.compressed, 0);
}

TEST(HTTP1xCodecTest, Test09Req) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback callbacks;
  codec.setCallback(&callbacks);
  auto buffer = folly::IOBuf::copyBuffer(string("GET /yeah\r\n"));
  codec.onIngress(*buffer);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.messageComplete, 1);
  EXPECT_EQ(buffer->length(), callbacks.headerSize.uncompressed);
  EXPECT_EQ(callbacks.headerSize.compressed, 0);
  buffer = folly::IOBuf::copyBuffer(string("\r\n"));
  codec.onIngress(*buffer);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.messageComplete, 1);
}

TEST(HTTP1xCodecTest, Test09ReqVers) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback callbacks;
  codec.setCallback(&callbacks);
  auto buffer = folly::IOBuf::copyBuffer(string("GET /yeah HTTP/0.9\r\n"));
  codec.onIngress(*buffer);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.messageComplete, 1);
  EXPECT_EQ(buffer->length(), callbacks.headerSize.uncompressed);
  EXPECT_EQ(callbacks.headerSize.compressed, 0);
}

TEST(HTTP1xCodecTest, Test09Resp) {
  HTTP1xCodec codec(TransportDirection::UPSTREAM);
  HTTP1xCodecCallback callbacks;
  HTTPMessage req;
  auto id = codec.createStream();
  req.setHTTPVersion(0, 9);
  req.setMethod(HTTPMethod::GET);
  req.setURL("/");
  codec.setCallback(&callbacks);
  folly::IOBufQueue buf;
  codec.generateHeader(buf, id, req, true);
  auto buffer = folly::IOBuf::copyBuffer(
    string("iamtheverymodelofamodernmajorgeneral"));
  codec.onIngress(*buffer);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.bodyLen, buffer->computeChainDataLength());
  EXPECT_EQ(callbacks.messageComplete, 0);
  codec.onIngressEOF();
  EXPECT_EQ(callbacks.messageComplete, 1);
}

TEST(HTTP1xCodecTest, TestBadHeaders) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  MockHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  auto buffer = folly::IOBuf::copyBuffer(
    string("GET /yeah HTTP/1.1\nUser-Agent: Mozilla/5.0 Version/4.0 "
           "\x10i\xC7n tho\xA1iSafari/534.30]"));
  EXPECT_CALL(callbacks, onMessageBegin(1, _));
  EXPECT_CALL(callbacks, onError(1, _, _))
    .WillOnce(Invoke([&] (HTTPCodec::StreamID,
                          std::shared_ptr<HTTPException> error,
                          bool) {
                       EXPECT_EQ(error->getHttpStatusCode(), 400);
        }));
  codec.onIngress(*buffer);
}

TEST(HTTP1xCodecTest, TestHeadRequestChunkedResponse) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback callbacks;
  codec.setCallback(&callbacks);
  auto txnID = codec.createStream();

  // Generate a HEAD request
  auto reqBuf = folly::IOBuf::copyBuffer(
      "HEAD /www.facebook.com HTTP/1.1\nHost: www.facebook.com\n\n");
  codec.onIngress(*reqBuf);
  EXPECT_EQ(callbacks.headersComplete, 1);

  // Generate chunked response with no body
  HTTPMessage resp;
  resp.setHTTPVersion(1, 1);
  resp.setStatusCode(200);
  resp.setIsChunked(true);
  resp.getHeaders().set(HTTP_HEADER_TRANSFER_ENCODING, "chunked");
  folly::IOBufQueue respBuf(folly::IOBufQueue::cacheChainLength());
  codec.generateHeader(respBuf, txnID, resp, true);
  auto respStr = respBuf.move()->moveToFbString();
  EXPECT_TRUE(respStr.find("0\r\n") == string::npos);
}

TEST(HTTP1xCodecTest, TestGetRequestChunkedResponse) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback callbacks;
  codec.setCallback(&callbacks);
  auto txnID = codec.createStream();

  // Generate a GET request
  auto reqBuf = folly::IOBuf::copyBuffer(
      "GET /www.facebook.com HTTP/1.1\nHost: www.facebook.com\n\n");
  codec.onIngress(*reqBuf);
  EXPECT_EQ(callbacks.headersComplete, 1);

  // Generate chunked response with body
  HTTPMessage resp;
  resp.setHTTPVersion(1, 1);
  resp.setStatusCode(200);
  resp.setIsChunked(true);
  resp.getHeaders().set(HTTP_HEADER_TRANSFER_ENCODING, "chunked");
  folly::IOBufQueue respBuf(folly::IOBufQueue::cacheChainLength());
  codec.generateHeader(respBuf, txnID, resp, false);

  auto headerFromBuf = respBuf.split(respBuf.chainLength());

  string resp1("Hello");
  auto body1 = folly::IOBuf::copyBuffer(resp1);

  string resp2("");
  auto body2 = folly::IOBuf::copyBuffer(resp2);

  codec.generateBody(respBuf, txnID, std::move(body1), HTTPCodec::NoPadding,
                     false);

  auto bodyFromBuf = respBuf.split(respBuf.chainLength());
  ASSERT_EQ("5\r\nHello\r\n", bodyFromBuf->moveToFbString());

  codec.generateBody(respBuf, txnID, std::move(body2), HTTPCodec::NoPadding,
                     true);

  bodyFromBuf = respBuf.split(respBuf.chainLength());
  ASSERT_EQ("0\r\n\r\n", bodyFromBuf->moveToFbString());
}

unique_ptr<folly::IOBuf> getChunkedRequest1st() {
  string req("GET /aha HTTP/1.1\n");
  return folly::IOBuf::copyBuffer(req);
}

unique_ptr<folly::IOBuf> getChunkedRequest2nd() {
  string req("Host: m.facebook.com\nAccept-Encoding: meflate\n\n");
  return folly::IOBuf::copyBuffer(req);
}

TEST(HTTP1xCodecTest, TestChunkedHeaders) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback callbacks;
  codec.setCallback(&callbacks);
  // test a sequence of requests to make sure we're resetting the size counter
  for (int i = 0; i < 3; i++) {
    callbacks.headersComplete = 0;
    auto buffer1 = getChunkedRequest1st();
    codec.onIngress(*buffer1);
    EXPECT_EQ(callbacks.headersComplete, 0);

    auto buffer2 = getChunkedRequest2nd();
    codec.onIngress(*buffer2);
    EXPECT_EQ(callbacks.headersComplete, 1);
    EXPECT_EQ(callbacks.headerSize.uncompressed,
              buffer1->length() + buffer2->length());
  }
}

TEST(HTTP1xCodecTest, TestChunkedUpstream) {
  HTTP1xCodec codec(TransportDirection::UPSTREAM);

  auto txnID = codec.createStream();

  HTTPMessage msg;
  msg.setHTTPVersion(1, 1);
  msg.setURL("https://www.facebook.com/");
  msg.getHeaders().set(HTTP_HEADER_HOST, "www.facebook.com");
  msg.getHeaders().set(HTTP_HEADER_TRANSFER_ENCODING, "chunked");
  msg.setIsChunked(true);

  HTTPHeaderSize size;

  folly::IOBufQueue buf(folly::IOBufQueue::cacheChainLength());
  codec.generateHeader(buf, txnID, msg, false, &size);
  auto headerFromBuf = buf.split(buf.chainLength());

  string req1("Hello");
  auto body1 = folly::IOBuf::copyBuffer(req1);

  string req2("World");
  auto body2 = folly::IOBuf::copyBuffer(req2);

  codec.generateBody(buf, txnID, std::move(body1), HTTPCodec::NoPadding,
                     false);

  auto bodyFromBuf = buf.split(buf.chainLength());
  ASSERT_EQ("5\r\nHello\r\n", bodyFromBuf->moveToFbString());

  codec.generateBody(buf, txnID, std::move(body2), HTTPCodec::NoPadding,
                     true);
  LOG(WARNING) << "len chain" << buf.chainLength();

  auto eomFromBuf = buf.split(buf.chainLength());
  ASSERT_EQ("5\r\nWorld\r\n0\r\n\r\n", eomFromBuf->moveToFbString());
}

TEST(HTTP1xCodecTest, TestBadPost100) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  MockHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  folly::IOBufQueue writeBuf(folly::IOBufQueue::cacheChainLength());

  InSequence enforceOrder;
  EXPECT_CALL(callbacks, onMessageBegin(1, _));
  EXPECT_CALL(callbacks, onHeadersComplete(1, _))
    .WillOnce(InvokeWithoutArgs([&] {
          HTTPMessage cont;
          cont.setStatusCode(100);
          cont.setStatusMessage("Continue");
          codec.generateHeader(writeBuf, 1, cont);
        }));

  EXPECT_CALL(callbacks, onBody(1, _, _));
  EXPECT_CALL(callbacks, onMessageComplete(1, _));
  EXPECT_CALL(callbacks, onMessageBegin(2, _))
    .WillOnce(InvokeWithoutArgs([&] {
          // simulate HTTPSession's aversion to pipelining
          codec.setParserPaused(true);

          // Trigger the response to the POST
          HTTPMessage resp;
          resp.setStatusCode(200);
          resp.setStatusMessage("OK");
          codec.generateHeader(writeBuf, 1, resp);
          codec.generateEOM(writeBuf, 1);
          codec.setParserPaused(false);
        }));
  EXPECT_CALL(callbacks, onError(2, _, _))
    .WillOnce(InvokeWithoutArgs([&] {
          HTTPMessage resp;
          resp.setStatusCode(400);
          resp.setStatusMessage("Bad");
          codec.generateHeader(writeBuf, 2, resp);
          codec.generateEOM(writeBuf, 2);
        }));
  // Generate a POST request with a bad content-length
  auto reqBuf = folly::IOBuf::copyBuffer(
      "POST /www.facebook.com HTTP/1.1\r\nHost: www.facebook.com\r\n"
      "Expect: 100-Continue\r\nContent-Length: 5\r\n\r\nabcdefghij");
  codec.onIngress(*reqBuf);
}

TEST(HTTP1xCodecTest, TestMultipleIdenticalContentLengthHeaders) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  folly::IOBufQueue writeBuf(folly::IOBufQueue::cacheChainLength());

  // Generate a POST request with two identical Content-Length headers
  auto reqBuf = folly::IOBuf::copyBuffer(
      "POST /www.facebook.com HTTP/1.1\r\nHost: www.facebook.com\r\n"
      "Content-Length: 5\r\nContent-Length: 5\r\n\r\n");
  codec.onIngress(*reqBuf);

  // Check that the request is accepted
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);

}

TEST(HTTP1xCodecTest, TestMultipleDistinctContentLengthHeaders) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  folly::IOBufQueue writeBuf(folly::IOBufQueue::cacheChainLength());

  // Generate a POST request with two distinct Content-Length headers
  auto reqBuf = folly::IOBuf::copyBuffer(
      "POST /www.facebook.com HTTP/1.1\r\nHost: www.facebook.com\r\n"
      "Content-Length: 5\r\nContent-Length: 6\r\n\r\n");
  codec.onIngress(*reqBuf);

  // Check that the request fails before the codec finishes parsing the headers
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.lastParseError->getHttpStatusCode(), 400);
}

TEST(HTTP1xCodecTest, TestCorrectTransferEncodingHeader) {
  HTTP1xCodec downstream(TransportDirection::DOWNSTREAM);
  FakeHTTPCodecCallback callbacks;
  downstream.setCallback(&callbacks);
  folly::IOBufQueue writeBuf(folly::IOBufQueue::cacheChainLength());

  // Generate a POST request with folded
  auto reqBuf = folly::IOBuf::copyBuffer(
      "POST /www.facebook.com HTTP/1.1\r\nHost: www.facebook.com\r\n"
      "Transfer-Encoding: chunked\r\n\r\n");
  downstream.onIngress(*reqBuf);

  // Check that the request fails before the codec finishes parsing the headers
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);
}

TEST(HTTP1xCodecTest, TestFoldedTransferEncodingHeader) {
  HTTP1xCodec downstream(TransportDirection::DOWNSTREAM);
  FakeHTTPCodecCallback callbacks;
  downstream.setCallback(&callbacks);
  folly::IOBufQueue writeBuf(folly::IOBufQueue::cacheChainLength());

  // Generate a POST request with folded
  auto reqBuf = folly::IOBuf::copyBuffer(
      "POST /www.facebook.com HTTP/1.1\r\nHost: www.facebook.com\r\n"
      "Transfer-Encoding: \r\n chunked\r\nContent-Length: 8\r\n\r\n");
  downstream.onIngress(*reqBuf);

  // Check that the request fails before the codec finishes parsing the headers
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.lastParseError->getHttpStatusCode(), 400);
}

TEST(HTTP1xCodecTest, TestBadTransferEncodingHeader) {
  HTTP1xCodec downstream(TransportDirection::DOWNSTREAM);
  FakeHTTPCodecCallback callbacks;
  downstream.setCallback(&callbacks);
  folly::IOBufQueue writeBuf(folly::IOBufQueue::cacheChainLength());

  auto reqBuf = folly::IOBuf::copyBuffer(
      "POST /www.facebook.com HTTP/1.1\r\nHost: www.facebook.com\r\n"
      "Transfer-Encoding: chunked, zorg\r\n\r\n");
  downstream.onIngress(*reqBuf);

  // Check that the request fails before the codec finishes parsing the headers
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.lastParseError->getHttpStatusCode(), 400);
}

TEST(HTTP1xCodecTest, Test1xxConnectionHeader) {
  HTTP1xCodec upstream(TransportDirection::UPSTREAM);
  HTTP1xCodec downstream(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback callbacks;
  upstream.setCallback(&callbacks);
  HTTPMessage resp;
  resp.setStatusCode(100);
  resp.setHTTPVersion(1, 1);
  resp.getHeaders().add(HTTP_HEADER_CONNECTION, "Upgrade");
  folly::IOBufQueue writeBuf(folly::IOBufQueue::cacheChainLength());
  auto streamID = downstream.createStream();
  downstream.generateHeader(writeBuf, streamID, resp);
  upstream.onIngress(*writeBuf.front());
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(
    callbacks.msg_->getHeaders().getSingleOrEmpty(HTTP_HEADER_CONNECTION),
    "Upgrade");
  resp.setStatusCode(200);
  resp.getHeaders().remove(HTTP_HEADER_CONNECTION);
  resp.getHeaders().add(HTTP_HEADER_CONTENT_LENGTH, "0");
  writeBuf.move();
  downstream.generateHeader(writeBuf, streamID, resp);
  upstream.onIngress(*writeBuf.front());
  EXPECT_EQ(callbacks.headersComplete, 2);
  EXPECT_EQ(
    callbacks.msg_->getHeaders().getSingleOrEmpty(HTTP_HEADER_CONNECTION),
    "keep-alive");
}

TEST(HTTP1xCodecTest, TestChainedBody) {
  HTTP1xCodec codec(TransportDirection::DOWNSTREAM);
  MockHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);

  folly::IOBufQueue bodyQueue;
  ON_CALL(callbacks, onBody(1, _, _))
      .WillByDefault(Invoke(
          [&bodyQueue](HTTPCodec::StreamID, std::shared_ptr<folly::IOBuf> buf,
                       uint16_t) { bodyQueue.append(buf->clone()); }));

  folly::IOBufQueue reqQueue;
  reqQueue.append(folly::IOBuf::copyBuffer(
      "POST /test.php HTTP/1.1\r\nHost: www.test.com\r\n"
      "Content-Length: 10\r\n\r\nabcde"));
  reqQueue.append(folly::IOBuf::copyBuffer("fghij"));

  while (!reqQueue.empty()) {
    auto processed = codec.onIngress(*reqQueue.front());
    if (processed == 0) {
      break;
    }
    reqQueue.trimStart(processed);
  }

  EXPECT_TRUE(folly::IOBufEqualTo()(*bodyQueue.front(),
                                  *folly::IOBuf::copyBuffer("abcdefghij")));
}

TEST(HTTP1xCodecTest, TestIgnoreUpstreamUpgrade) {
  HTTP1xCodec codec(TransportDirection::UPSTREAM);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  folly::IOBufQueue writeBuf(folly::IOBufQueue::cacheChainLength());

  auto reqBuf = folly::IOBuf::copyBuffer(
      "HTTP/1.1 200 OK\r\n"
      "Connection: close\r\n"
      "Upgrade: h2,h2c\r\n"
      "\r\n"
      "<!DOCTYPE html>");
  codec.onIngress(*reqBuf);

  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.bodyLength, 15);
}

TEST(HTTP1xCodecTest, WebsocketUpgrade) {
  HTTP1xCodec upstreamCodec(TransportDirection::UPSTREAM);
  HTTP1xCodec downstreamCodec(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback downStreamCallbacks;
  HTTP1xCodecCallback upstreamCallbacks;
  downstreamCodec.setCallback(&downStreamCallbacks);
  upstreamCodec.setCallback(&upstreamCallbacks);

  HTTPMessage req;
  req.setHTTPVersion(1, 1);
  req.setURL("/websocket");
  req.setEgressWebsocketUpgrade();
  folly::IOBufQueue buf;
  auto streamID = upstreamCodec.createStream();
  upstreamCodec.generateHeader(buf, streamID, req);

  downstreamCodec.onIngress(*buf.front());
  EXPECT_EQ(downStreamCallbacks.headersComplete, 1);
  EXPECT_TRUE(downStreamCallbacks.msg_->isIngressWebsocketUpgrade());
  auto& headers = downStreamCallbacks.msg_->getHeaders();
  auto ws_key_header = headers.getSingleOrEmpty(HTTP_HEADER_SEC_WEBSOCKET_KEY);
  EXPECT_NE(ws_key_header, empty_string);

  HTTPMessage resp;
  resp.setHTTPVersion(1, 1);
  resp.setStatusCode(101);
  resp.setEgressWebsocketUpgrade();
  buf.clear();
  downstreamCodec.generateHeader(buf, streamID, resp);
  upstreamCodec.onIngress(*buf.front());
  EXPECT_EQ(upstreamCallbacks.headersComplete, 1);
  headers = upstreamCallbacks.msg_->getHeaders();
  auto ws_accept_header =
    headers.getSingleOrEmpty(HTTP_HEADER_SEC_WEBSOCKET_ACCEPT);
  EXPECT_NE(ws_accept_header, empty_string);
}

TEST(HTTP1xCodecTest, WebsocketUpgradeKeyError) {
  HTTP1xCodec codec(TransportDirection::UPSTREAM);
  HTTP1xCodecCallback callbacks;
  codec.setCallback(&callbacks);

  HTTPMessage req;
  req.setHTTPVersion(1, 1);
  req.setURL("/websocket");
  req.setEgressWebsocketUpgrade();
  folly::IOBufQueue buf;
  auto streamID = codec.createStream();
  codec.generateHeader(buf, streamID, req);

  auto resp = folly::IOBuf::copyBuffer(
      "HTTP/1.1 200 OK\r\n"
      "Connection: upgrade\r\n"
      "Upgrade: websocket\r\n"
      "\r\n");
  codec.onIngress(*resp);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.errors, 1);
}

TEST(HTTP1xCodecTest, WebsocketUpgradeHeaderSet) {
  HTTP1xCodec upstreamCodec(TransportDirection::UPSTREAM);
  HTTPMessage req;
  req.setMethod(HTTPMethod::GET);
  req.setURL("/websocket");
  req.setEgressWebsocketUpgrade();
  req.getHeaders().add(proxygen::HTTP_HEADER_UPGRADE, "Websocket");

  folly::IOBufQueue buf;
  upstreamCodec.generateHeader(buf, upstreamCodec.createStream(), req);

  HTTP1xCodec downstreamCodec(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback callbacks;
  downstreamCodec.setCallback(&callbacks);
  downstreamCodec.onIngress(*buf.front());
  auto headers = callbacks.msg_->getHeaders();
  EXPECT_EQ(headers.getSingleOrEmpty(HTTP_HEADER_SEC_WEBSOCKET_KEY),
      empty_string);
}

TEST(HTTP1xCodecTest, WebsocketConnectionHeader) {
  HTTP1xCodec upstreamCodec(TransportDirection::UPSTREAM);
  HTTPMessage req;
  req.setMethod(HTTPMethod::GET);
  req.setURL("/websocket");
  req.setEgressWebsocketUpgrade();
  req.getHeaders().add(proxygen::HTTP_HEADER_CONNECTION, "upgrade, keep-alive");
  req.getHeaders().add(proxygen::HTTP_HEADER_SEC_WEBSOCKET_KEY,
      "key should change");
  req.getHeaders().add(proxygen::HTTP_HEADER_SEC_WEBSOCKET_ACCEPT,
      "this should not be found");

  folly::IOBufQueue buf;
  upstreamCodec.generateHeader(buf, upstreamCodec.createStream(), req);
  HTTP1xCodec downstreamCodec(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback callbacks;
  downstreamCodec.setCallback(&callbacks);
  downstreamCodec.onIngress(*buf.front());
  auto headers = callbacks.msg_->getHeaders();
  auto ws_sec_key = headers.getSingleOrEmpty(HTTP_HEADER_SEC_WEBSOCKET_KEY);
  EXPECT_NE(ws_sec_key, empty_string);
  EXPECT_NE(ws_sec_key, "key should change");

  // We know the key is length 16
  // https://tools.ietf.org/html/rfc6455#section-4.2.1.5
  // for base64 % 3 leaves 1 byte so we expect padding of '=='
  // hence this offset of 2 explicitly
  EXPECT_NO_THROW(Base64::decode(ws_sec_key, 2));
  EXPECT_EQ(16, Base64::decode(ws_sec_key, 2).length());

  EXPECT_EQ(headers.getSingleOrEmpty(HTTP_HEADER_SEC_WEBSOCKET_ACCEPT),
      empty_string);
  EXPECT_EQ(headers.getSingleOrEmpty(HTTP_HEADER_CONNECTION),
      "upgrade, keep-alive");
}

TEST(HTTP1xCodecTest, TrailersAndEomAreNotGeneratedWhenNonChunked) {
  // Verify that generateTrailes and generateEom result in 0 bytes
  // generated when message is not chunked.
  // HTTP2 codec handles all BODY as regular non-chunked body, thus
  // HTTPTransation SM transitions allow trailers after regular body. Which
  // is not allowed in HTTP1.
  HTTP1xCodec codec(TransportDirection::UPSTREAM);

  auto txnID = codec.createStream();

  HTTPMessage msg;
  msg.setHTTPVersion(1, 1);
  msg.setURL("https://www.facebook.com/");
  msg.getHeaders().set(HTTP_HEADER_HOST, "www.facebook.com");
  msg.setIsChunked(false);

  folly::IOBufQueue buf;
  codec.generateHeader(buf, txnID, msg);

  HTTPHeaders trailers;
  trailers.add("X-Test-Trailer", "test");
  EXPECT_EQ(0, codec.generateTrailers(buf, txnID, trailers));
  EXPECT_EQ(0, codec.generateEOM(buf, txnID));
}

class ConnectionHeaderTest:
    public TestWithParam<std::pair<std::list<string>, string>> {
 public:
  using ParamType = std::pair<std::list<string>, string>;
};

TEST_P(ConnectionHeaderTest, TestConnectionHeaders) {
  HTTP1xCodec upstream(TransportDirection::UPSTREAM);
  HTTP1xCodec downstream(TransportDirection::DOWNSTREAM);
  HTTP1xCodecCallback callbacks;
  downstream.setCallback(&callbacks);
  HTTPMessage req;
  req.setMethod(HTTPMethod::GET);
  req.setURL("/");
  auto val = GetParam();
  for (auto header: val.first) {
    req.getHeaders().add(HTTP_HEADER_CONNECTION, header);
  }
  folly::IOBufQueue writeBuf(folly::IOBufQueue::cacheChainLength());
  upstream.generateHeader(writeBuf, upstream.createStream(), req);
  downstream.onIngress(*writeBuf.front());
  EXPECT_EQ(callbacks.headersComplete, 1);
  auto& headers = callbacks.msg_->getHeaders();
  EXPECT_EQ(headers.getSingleOrEmpty(HTTP_HEADER_CONNECTION),
            val.second);
}


INSTANTIATE_TEST_CASE_P(
  HTTP1xCodec,
  ConnectionHeaderTest,
  ::testing::Values(
    // Moves close to the end
    ConnectionHeaderTest::ParamType(
      { "foo", "bar", "close", "baz" }, "foo, bar, baz, close"),
    // has to resize token vector
    ConnectionHeaderTest::ParamType(
      { "foo", "bar, close", "baz" }, "foo, bar, baz, close"),
    // whitespace trimming
    ConnectionHeaderTest::ParamType(
      { " foo", "bar, close ", " baz " }, "foo, bar, baz, close"),
    // No close token => keep-alive
    ConnectionHeaderTest::ParamType(
      { "foo", "bar, boo", "baz" }, "foo, bar, boo, baz, keep-alive"),
    // close and keep-alive => close
    ConnectionHeaderTest::ParamType(
      { "foo", "keep-alive, boo", "close" }, "foo, boo, close"),
    // upgrade gets no special treatment
    ConnectionHeaderTest::ParamType(
      { "foo", "upgrade, boo", "baz" }, "foo, upgrade, boo, baz, keep-alive")
  ));
