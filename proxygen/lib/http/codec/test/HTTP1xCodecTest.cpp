/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>

using namespace proxygen;
using namespace std;

class HTTP1xCodecCallback : public HTTPCodec::Callback {
 public:
  HTTP1xCodecCallback() {}

  void onMessageBegin(HTTPCodec::StreamID stream, HTTPMessage* msg) override {}
  void onPushMessageBegin(HTTPCodec::StreamID stream,
                          HTTPCodec::StreamID assocStream,
                          HTTPMessage* msg) override {}
  void onHeadersComplete(HTTPCodec::StreamID stream,
                         std::unique_ptr<HTTPMessage> msg) override {
    headersComplete++;
    headerSize = msg->getIngressHeaderSize();
  }
  void onBody(HTTPCodec::StreamID stream,
              std::unique_ptr<folly::IOBuf> chain,
              uint16_t padding) override {}
  void onChunkHeader(HTTPCodec::StreamID stream, size_t length) override {}
  void onChunkComplete(HTTPCodec::StreamID stream) override {}
  void onTrailersComplete(HTTPCodec::StreamID stream,
                          std::unique_ptr<HTTPHeaders> trailers) override {}
  void onMessageComplete(HTTPCodec::StreamID stream, bool upgrade) override {}
  void onError(HTTPCodec::StreamID stream,
               const HTTPException& error,
               bool newTxn) override {
    LOG(ERROR) << "parse error";
  }

  uint32_t headersComplete{0};
  HTTPHeaderSize headerSize;
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
  codec.generateHeader(respBuf, txnID, resp, 0, true);
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
  codec.generateHeader(respBuf, txnID, resp, 0, false);

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
  msg.getHeaders().set("Host", "www.facebook.com");
  msg.getHeaders().set("Transfer-Encoding", "chunked");
  msg.setIsChunked(true);

  HTTPHeaderSize size;

  folly::IOBufQueue buf(folly::IOBufQueue::cacheChainLength());
  codec.generateHeader(buf, txnID, msg, 0, false, &size);
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
