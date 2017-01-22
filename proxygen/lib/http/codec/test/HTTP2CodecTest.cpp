/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/test/HTTPParallelCodecTest.h>
#include <folly/io/Cursor.h>
#include <proxygen/lib/http/codec/HTTP2Codec.h>
#include <proxygen/lib/http/codec/test/HTTP2FramerTest.h>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/utils/ChromeUtils.h>

#include <folly/portability/GTest.h>
#include <random>

using namespace proxygen;
using namespace folly;
using namespace folly::io;
using namespace std;

class HTTP2CodecTest : public HTTPParallelCodecTest {
 public:

  HTTP2CodecTest()
    :HTTPParallelCodecTest(upstreamCodec_, downstreamCodec_) {}

  void SetUp() override {
    HTTP2Codec::setHeaderSplitSize(http2::kMaxFramePayloadLengthMin);
    HTTPParallelCodecTest::SetUp();
  }

  void testBigHeader(bool continuation);


 protected:
  HTTP2Codec upstreamCodec_{TransportDirection::UPSTREAM};
  HTTP2Codec downstreamCodec_{TransportDirection::DOWNSTREAM};
};

TEST_F(HTTP2CodecTest, BasicHeader) {
  HTTPMessage req = getGetRequest("/guacamole");
  req.getHeaders().add("user-agent", "coolio");
  req.getHeaders().add("tab-hdr", "coolio\tv2");
  // Connection header will get dropped
  req.getHeaders().add("Connection", "Love");
  req.setSecure(true);
  upstreamCodec_.generateHeader(output_, 1, req, 0, true /* eom */);

  parse();
  callbacks_.expectMessage(true, 3, "/guacamole");
  EXPECT_TRUE(callbacks_.msg->isSecure());
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_EQ("coolio", headers.getSingleOrEmpty("user-agent"));
  EXPECT_EQ("coolio\tv2", headers.getSingleOrEmpty("tab-hdr"));
  EXPECT_EQ("www.foo.com", headers.getSingleOrEmpty("host"));
}

TEST_F(HTTP2CodecTest, BadHeaders) {
  static const std::string v1("GET");
  static const std::string v2("/");
  static const std::string v3("http");
  static const std::string v4("foo.com");
  static const vector<proxygen::compress::Header> reqHeaders = {
    { http2::kMethod, v1 },
    { http2::kPath, v2 },
    { http2::kScheme, v3 },
    { http2::kAuthority, v4 },
  };

  HPACKCodec headerCodec(TransportDirection::UPSTREAM);
  HTTPCodec::StreamID stream = 1;
  // missing fields (missing authority is OK)
  for (size_t i = 0; i < reqHeaders.size(); i++, stream += 2) {
    std::vector<proxygen::compress::Header> allHeaders = reqHeaders;
    allHeaders.erase(allHeaders.begin() + i);
    auto encodedHeaders = headerCodec.encode(allHeaders);
    http2::writeHeaders(output_,
                        std::move(encodedHeaders),
                        stream,
                        boost::none,
                        boost::none,
                        true,
                        true);
  }
  // dup fields
  std::string v("foomonkey");
  for (size_t i = 0; i < reqHeaders.size(); i++, stream += 2) {
    std::vector<proxygen::compress::Header> allHeaders = reqHeaders;
    auto h = allHeaders[i];
    h.value = &v;
    allHeaders.push_back(h);
    auto encodedHeaders = headerCodec.encode(allHeaders);
    http2::writeHeaders(output_,
                        std::move(encodedHeaders),
                        stream,
                        boost::none,
                        boost::none,
                        true,
                        true);
  }

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.headersComplete, 1);
  EXPECT_EQ(callbacks_.messageComplete, 1);
  EXPECT_EQ(callbacks_.streamErrors, 7);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BadPseudoHeaders) {
  static const std::string v1("POST");
  static const std::string v2("http");
  static const std::string n3("foo");
  static const std::string v3("bar");
  static const std::string v4("/");
  static const vector<proxygen::compress::Header> reqHeaders = {
    { http2::kMethod, v1 },
    { http2::kScheme, v2 },
    { n3, v3 },
    { http2::kPath, v4 },
  };

  HPACKCodec headerCodec(TransportDirection::UPSTREAM);
  HTTPCodec::StreamID stream = 1;
  std::vector<proxygen::compress::Header> allHeaders = reqHeaders;
  auto encodedHeaders = headerCodec.encode(allHeaders);
  http2::writeHeaders(output_,
                      std::move(encodedHeaders),
                      stream,
                      boost::none,
                      boost::none,
                      true,
                      true);

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 1);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BadHeaderValues) {
  static const std::string v1("--1");
  static const std::string v2("\13\10protocol-attack");
  static const std::string v3("\13");
  static const std::string v4("abc.com\\13\\10");
  static const vector<proxygen::compress::Header> reqHeaders = {
    { http2::kMethod, v1 },
    { http2::kPath, v2 },
    { http2::kScheme, v3 },
    { http2::kAuthority, v4 },
  };

  HPACKCodec headerCodec(TransportDirection::UPSTREAM);
  HTTPCodec::StreamID stream = 1;
  for (size_t i = 0; i < reqHeaders.size(); i++, stream += 2) {
    std::vector<proxygen::compress::Header> allHeaders;
    allHeaders.push_back(reqHeaders[i]);
    auto encodedHeaders = headerCodec.encode(allHeaders);
    http2::writeHeaders(output_,
                        std::move(encodedHeaders),
                        stream,
                        boost::none,
                        boost::none,
                        true,
                        true);
  }

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 4);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, DuplicateHeaders) {
  HTTPMessage req = getGetRequest("/guacamole");
  req.getHeaders().add("user-agent", "coolio");
  req.setSecure(true);
  upstreamCodec_.generateHeader(output_, 1, req, 0, true /* eom */);
  writeFrameHeaderManual(output_, 0, (uint8_t)http2::FrameType::HEADERS,
                         http2::END_STREAM, 1);

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.headersComplete, 1);
  EXPECT_EQ(callbacks_.messageComplete, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
}


/**
 * Ingress bytes with an empty header name
 */
const uint8_t kBufEmptyHeader[] = {
  0x00, 0x00, 0x1d, 0x01, 0x04, 0x00, 0x00, 0x00, 0x01, 0x82,
  0x87, 0x44, 0x87, 0x62, 0x6b, 0x46, 0x41, 0xd2, 0x7a, 0x0b,
  0x41, 0x89, 0xf1, 0xe3, 0xc2, 0xf2, 0x9c, 0xeb, 0x90, 0xf4,
  0xff, 0x40, 0x80, 0x84, 0x2d, 0x35, 0xa7, 0xd7
};

TEST_F(HTTP2CodecTest, EmptyHeaderName) {
  output_.append(IOBuf::copyBuffer(kBufEmptyHeader, sizeof(kBufEmptyHeader)));
  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 1);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BasicConnect) {
  std::string authority = "myhost:1234";
  HTTPMessage request;
  request.setMethod(HTTPMethod::CONNECT);
  request.getHeaders().add(proxygen::HTTP_HEADER_HOST, authority);
  upstreamCodec_.generateHeader(output_, 1, request, 0, false /* eom */);

  parse();
  callbacks_.expectMessage(false, 1, "");
  EXPECT_EQ(HTTPMethod::CONNECT, callbacks_.msg->getMethod());
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_EQ(authority, headers.getSingleOrEmpty(proxygen::HTTP_HEADER_HOST));
}

TEST_F(HTTP2CodecTest, BadConnect) {
  std::string v1 = "CONNECT";
  std::string v2 = "somehost:576";
  std::vector<proxygen::compress::Header> goodHeaders = {
    { http2::kMethod, v1 },
    { http2::kAuthority, v2 },
  };

  // See https://tools.ietf.org/html/rfc7540#section-8.3
  std::string v3 = "/foobar";
  std::vector<proxygen::compress::Header> badHeaders = {
    { http2::kScheme, http2::kHttp },
    { http2::kPath, v3 },
  };

  HPACKCodec headerCodec(TransportDirection::UPSTREAM);
  HTTPCodec::StreamID stream = 1;

  for (size_t i = 0; i < badHeaders.size(); i++, stream += 2) {
    auto allHeaders = goodHeaders;
    allHeaders.push_back(badHeaders[i]);
    auto encodedHeaders = headerCodec.encode(allHeaders);
    http2::writeHeaders(output_,
                        std::move(encodedHeaders),
                        stream,
                        boost::none,
                        boost::none,
                        true,
                        true);
  }

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, badHeaders.size());
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

void HTTP2CodecTest::testBigHeader(bool continuation) {
  if (continuation) {
    HTTP2Codec::setHeaderSplitSize(1);
  }
  auto settings = downstreamCodec_.getEgressSettings();
  settings->setSetting(SettingsId::MAX_HEADER_LIST_SIZE, 37);
  IOBufQueue dummy;
  downstreamCodec_.generateSettings(dummy);
  HTTPMessage req = getGetRequest("/guacamole");
  req.getHeaders().add("user-agent", "coolio");
  req.getHeaders().add("x-long-long-header",
                       "supercalafragalisticexpialadoshus");
  upstreamCodec_.generateHeader(output_, 1, req, 0, true /* eom */);

  parse();
  // session error
  EXPECT_EQ(callbacks_.messageBegin, continuation ? 1 : 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
}

TEST_F(HTTP2CodecTest, BigHeader) {
  testBigHeader(false);
}

TEST_F(HTTP2CodecTest, BigHeaderContinuation) {
  testBigHeader(true);
}

TEST_F(HTTP2CodecTest, BasicHeaderReply) {
  SetUpUpstreamTest();
  HTTPMessage resp;
  resp.setStatusCode(200);
  resp.setStatusMessage("nifty-nice");
  resp.getHeaders().add("content-type", "x-coolio");
  downstreamCodec_.generateHeader(output_, 1, resp, 0);
  downstreamCodec_.generateEOM(output_, 1);

  parseUpstream();
  callbacks_.expectMessage(true, 2, 200);
  const auto& headers = callbacks_.msg->getHeaders();
  // HTTP/2 doesnt support serialization - instead you get the default
  EXPECT_EQ("OK", callbacks_.msg->getStatusMessage());
  EXPECT_TRUE(callbacks_.msg->getHeaders().exists(HTTP_HEADER_DATE));
  EXPECT_EQ("x-coolio", headers.getSingleOrEmpty("content-type"));
}

TEST_F(HTTP2CodecTest, BadHeadersReply) {
  static const std::string v1("200");
  static const vector<proxygen::compress::Header> respHeaders = {
    { http2::kStatus, v1 },
  };

  HPACKCodec headerCodec(TransportDirection::DOWNSTREAM);
  HTTPCodec::StreamID stream = 1;
  // missing fields (missing authority is OK)
  for (size_t i = 0; i < respHeaders.size(); i++, stream += 2) {
    std::vector<proxygen::compress::Header> allHeaders = respHeaders;
    allHeaders.erase(allHeaders.begin() + i);
    auto encodedHeaders = headerCodec.encode(allHeaders);
    http2::writeHeaders(output_,
                        std::move(encodedHeaders),
                        stream,
                        boost::none,
                        boost::none,
                        true,
                        true);
  }
  // dup fields
  std::string v("foomonkey");
  for (size_t i = 0; i < respHeaders.size(); i++, stream += 2) {
    std::vector<proxygen::compress::Header> allHeaders = respHeaders;
    auto h = allHeaders[i];
    h.value = &v;
    allHeaders.push_back(h);
    auto encodedHeaders = headerCodec.encode(allHeaders);
    http2::writeHeaders(output_,
                        std::move(encodedHeaders),
                        stream,
                        boost::none,
                        boost::none,
                        true,
                        true);
  }

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 2);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, Cookies) {
  HTTPMessage req = getGetRequest("/guacamole");
  req.getHeaders().add("Cookie", "chocolate-chip=1");
  req.getHeaders().add("Cookie", "rainbow-chip=2");
  req.getHeaders().add("Cookie", "butterscotch=3");
  req.getHeaders().add("Cookie", "oatmeal-raisin=4");
  req.setSecure(true);
  upstreamCodec_.generateHeader(output_, 1, req, 0);

  parse();
  callbacks_.expectMessage(false, 2, "/guacamole");
  EXPECT_EQ(callbacks_.msg->getCookie("chocolate-chip"), "1");
  EXPECT_EQ(callbacks_.msg->getCookie("rainbow-chip"), "2");
  EXPECT_EQ(callbacks_.msg->getCookie("butterscotch"), "3");
  EXPECT_EQ(callbacks_.msg->getCookie("oatmeal-raisin"), "4");
}

TEST_F(HTTP2CodecTest, BasicContinuation) {
  HTTPMessage req = getGetRequest();
  req.getHeaders().add("user-agent", "coolio");
  HTTP2Codec::setHeaderSplitSize(1);
  upstreamCodec_.generateHeader(output_, 1, req, 0);

  parse();
  callbacks_.expectMessage(false, -1, "/");
#ifndef NDEBUG
  EXPECT_GT(downstreamCodec_.getReceivedFrameCount(), 1);
#endif
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_EQ("coolio", headers.getSingleOrEmpty("user-agent"));
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.headersComplete, 1);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BasicContinuationEndStream) {
  // CONTINUATION with END_STREAM flag set on the preceding HEADERS frame
  HTTPMessage req = getGetRequest();
  req.getHeaders().add("user-agent", "coolio");
  HTTP2Codec::setHeaderSplitSize(1);
  upstreamCodec_.generateHeader(output_, 1, req, 0, true /* eom */);

  parse();
  callbacks_.expectMessage(true, -1, "/");
#ifndef NDEBUG
  EXPECT_GT(downstreamCodec_.getReceivedFrameCount(), 1);
#endif
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_EQ("coolio", headers.getSingleOrEmpty("user-agent"));
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.headersComplete, 1);
  EXPECT_EQ(callbacks_.messageComplete, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BadContinuation) {
  // CONTINUATION with no preceding HEADERS
  auto fakeHeaders = makeBuf(5);
  http2::writeContinuation(output_, 3, true, std::move(fakeHeaders),
                           http2::kNoPadding);

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
}

TEST_F(HTTP2CodecTest, MissingContinuation) {
  IOBufQueue output(IOBufQueue::cacheChainLength());
  HTTPMessage req = getGetRequest();
  req.getHeaders().add("user-agent", "coolio");

  // empirically determined the header block will be 20 bytes, so split at N-1
  HTTP2Codec::setHeaderSplitSize(19);
  size_t prevLen = output_.chainLength();
  upstreamCodec_.generateHeader(output_, 1, req, 0);
  EXPECT_EQ(output_.chainLength() - prevLen, 20 + 2 * 9);
  // strip the continuation frame (1 byte payload)
  output_.trimEnd(http2::kFrameHeaderSize + 1);

  // insert a non-continuation (but otherwise valid) frame
  http2::writeGoaway(output_, 17, ErrorCode::ENHANCE_YOUR_CALM);

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
#ifndef NDEBUG
  EXPECT_EQ(downstreamCodec_.getReceivedFrameCount(), 2);
#endif
}

TEST_F(HTTP2CodecTest, MissingContinuationBadFrame) {
  IOBufQueue output(IOBufQueue::cacheChainLength());
  HTTPMessage req = getGetRequest();
  req.getHeaders().add("user-agent", "coolio");

  // empirically determined the header block will be 20 bytes, so split at N-1
  HTTP2Codec::setHeaderSplitSize(19);
  size_t prevLen = output_.chainLength();
  upstreamCodec_.generateHeader(output_, 1, req, 0);
  EXPECT_EQ(output_.chainLength() - prevLen, 20 + 2 * 9);
  // strip the continuation frame (1 byte payload)
  output_.trimEnd(http2::kFrameHeaderSize + 1);

  // insert an invalid frame
  auto frame = makeBuf(9);
  *((uint32_t *)frame->writableData()) = 0xfa000000;
  output_.append(std::move(frame));

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
#ifndef NDEBUG
  EXPECT_EQ(downstreamCodec_.getReceivedFrameCount(), 2);
#endif
}

TEST_F(HTTP2CodecTest, BadContinuationStream) {
  HTTPMessage req = getGetRequest();
  req.getHeaders().add("user-agent", "coolio");

  // empirically determined the header block will be 16 bytes, so split at N-1
  HTTP2Codec::setHeaderSplitSize(15);
  upstreamCodec_.generateHeader(output_, 1, req, 0);
  // strip the continuation frame (1 byte payload)
  output_.trimEnd(http2::kFrameHeaderSize + 1);

  auto fakeHeaders = makeBuf(1);
  http2::writeContinuation(output_, 3, true, std::move(fakeHeaders),
                           http2::kNoPadding);

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
#ifndef NDEBUG
  EXPECT_EQ(downstreamCodec_.getReceivedFrameCount(), 2);
#endif
}

TEST_F(HTTP2CodecTest, FrameTooLarge) {
  writeFrameHeaderManual(output_, 1 << 15, 0, 0, 1);

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
  EXPECT_TRUE(callbacks_.lastParseError->hasCodecStatusCode());
  EXPECT_EQ(callbacks_.lastParseError->getCodecStatusCode(),
            ErrorCode::FRAME_SIZE_ERROR);
}

TEST_F(HTTP2CodecTest, UnknownFrameType) {
  HTTPMessage req = getGetRequest();
  req.getHeaders().add("user-agent", "coolio");

  // unknown frame type 17
  writeFrameHeaderManual(output_, 17, 37, 0, 1);
  output_.append("wicked awesome!!!");
  upstreamCodec_.generateHeader(output_, 1, req, 0);

  parse();
  callbacks_.expectMessage(false, 2, ""); // + host
}

TEST_F(HTTP2CodecTest, JunkAfterConnError) {
  HTTPMessage req = getGetRequest();
  req.getHeaders().add("user-agent", "coolio");

  // write headers frame for stream 0
  writeFrameHeaderManual(output_, 0, (uint8_t)http2::FrameType::HEADERS, 0, 0);
  // now write a valid headers frame, should never be parsed
  upstreamCodec_.generateHeader(output_, 1, req, 0);

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
}

TEST_F(HTTP2CodecTest, BasicData) {
  string data("abcde");
  auto buf = folly::IOBuf::copyBuffer(data.data(), data.length());
  upstreamCodec_.generateBody(output_, 2, std::move(buf),
                              HTTPCodec::NoPadding, true);

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 1);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, 5);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
  EXPECT_EQ(callbacks_.data.move()->moveToFbString(), data);
}

TEST_F(HTTP2CodecTest, LongData) {
  // Hack the max frame size artificially low
  HTTPSettings* settings = (HTTPSettings*)upstreamCodec_.getIngressSettings();
  settings->setSetting(SettingsId::MAX_FRAME_SIZE, 16);
  auto buf = makeBuf(100);
  upstreamCodec_.generateBody(output_, 1, buf->clone(), HTTPCodec::NoPadding,
                              true);

  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 1);
  EXPECT_EQ(callbacks_.bodyCalls, 7);
  EXPECT_EQ(callbacks_.bodyLength, 100);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
  EXPECT_EQ(callbacks_.data.move()->moveToFbString(), buf->moveToFbString());
}

TEST_F(HTTP2CodecTest, MalformedPaddingLength) {
  const uint8_t badInput[] = {0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54,
                              0x54, 0x50, 0x2f, 0x32, 0x2e, 0x30, 0x0d, 0x0a,
                              0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a,
                              0x00, 0x00, 0x7e, 0x00, 0x6f, 0x6f, 0x6f, 0x6f,
                              // The padding length byte below is 0x82 (130
                              // in decimal) which is greater than the length
                              // specified by the header's length field, 126
                              0x01, 0x82, 0x87, 0x44, 0x87, 0x92, 0x97, 0x92,
                              0x92, 0x92, 0x7a, 0x0b, 0x41, 0x89, 0xf1, 0xe3,
                              0xc0, 0xf2, 0x9c, 0xdd, 0x90, 0xf4, 0xff, 0x40,
                              0x80, 0x84, 0x2d, 0x35, 0xa7, 0xd7};
  output_.clear();
  output_.append(badInput, sizeof(badInput));
  EXPECT_EQ(output_.chainLength(), sizeof(badInput));

  bool caughtException = false;
  bool parseResult = true;
  try {
    parseResult = parse();
  } catch (const std::exception &e) {
    caughtException = true;
  }
  EXPECT_FALSE(caughtException);
  EXPECT_FALSE(parseResult);
}

TEST_F(HTTP2CodecTest, NoAppByte) {
  const uint8_t noAppByte[] = {0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54,
                               0x54, 0x50, 0x2f, 0x32, 0x2e, 0x30, 0x0d, 0x0a,
                               0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a,
                               0x00, 0x00, 0x56, 0x00, 0x5d, 0x00, 0x00, 0x00,
                               0x01, 0x55, 0x00};
  output_.clear();
  output_.append(noAppByte, sizeof(noAppByte));
  EXPECT_EQ(output_.chainLength(), sizeof(noAppByte));

  bool caughtException = false;
  bool parseResult = false;
  try {
    parseResult = parse();
  } catch (const std::exception &e) {
    caughtException = true;
  }
  EXPECT_FALSE(caughtException);
  EXPECT_TRUE(parseResult);
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, DataFramePartialDataWithNoAppByte) {
  const size_t bufSize = 10;
  auto buf = makeBuf(bufSize);
  const size_t padding = 10;
  upstreamCodec_.generateBody(output_, 1, buf->clone(), padding, true);
  EXPECT_EQ(output_.chainLength(), 54);

  auto ingress = output_.move();
  ingress->coalesce();
  // Copy up to the padding length byte to a new buffer
  auto ingress1 = IOBuf::copyBuffer(ingress->data(), 34);
  size_t parsed = downstreamCodec_.onIngress(*ingress1);
  // The 34th byte is the padding length byte which should not be parsed
  EXPECT_EQ(parsed, 33);
  // Copy from the padding length byte to the end
  auto ingress2 = IOBuf::copyBuffer(ingress->data() + 33, 21);
  parsed = downstreamCodec_.onIngress(*ingress2);
  // The padding length byte should be parsed this time along with 10 bytes of
  // application data and 10 bytes of padding
  EXPECT_EQ(parsed, 21);

  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 1);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, bufSize);
  // Total padding is the padding length byte and the padding bytes
  EXPECT_EQ(callbacks_.paddingBytes, padding + 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
  EXPECT_EQ(callbacks_.data.move()->moveToFbString(), buf->moveToFbString());
}

TEST_F(HTTP2CodecTest, BasicRst) {
  upstreamCodec_.generateRstStream(output_, 2, ErrorCode::ENHANCE_YOUR_CALM);
  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.bodyCalls, 0);
  EXPECT_EQ(callbacks_.aborts, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BasicRstInvalidCode) {
  upstreamCodec_.generateRstStream(output_, 2, ErrorCode::_SPDY_INVALID_STREAM);
  parse();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.bodyCalls, 0);
  EXPECT_EQ(callbacks_.aborts, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BasicPing) {
  upstreamCodec_.generatePingRequest(output_);
  upstreamCodec_.generatePingReply(output_, 17);

  uint64_t pingReq;
  parse([&] (IOBuf* ingress) {
      folly::io::Cursor c(ingress);
      c.skip(http2::kFrameHeaderSize + http2::kConnectionPreface.length());
      pingReq = c.read<uint64_t>();
    });

  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.bodyCalls, 0);
  EXPECT_EQ(callbacks_.recvPingRequest, pingReq);
  EXPECT_EQ(callbacks_.recvPingReply, 17);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BasicWindow) {
  // This test would fail if the codec had window state
  upstreamCodec_.generateWindowUpdate(output_, 0, 10);
  upstreamCodec_.generateWindowUpdate(output_, 0, http2::kMaxWindowUpdateSize);
  upstreamCodec_.generateWindowUpdate(output_, 1, 12);
  upstreamCodec_.generateWindowUpdate(output_, 1, http2::kMaxWindowUpdateSize);

  parse();
  EXPECT_EQ(callbacks_.windowUpdateCalls, 4);
  EXPECT_EQ(callbacks_.windowUpdates[0],
            std::vector<uint32_t>({10, http2::kMaxWindowUpdateSize}));
  EXPECT_EQ(callbacks_.windowUpdates[1],
            std::vector<uint32_t>({12, http2::kMaxWindowUpdateSize}));
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, ZeroWindow) {
  auto streamID = HTTPCodec::StreamID(1);
  // First generate a frame with delta=1 so as to pass the checks, and then
  // hack the frame so that delta=0 without modifying other checks
  upstreamCodec_.generateWindowUpdate(output_, streamID, 1);
  output_.trimEnd(http2::kFrameWindowUpdateSize);
  QueueAppender appender(&output_, http2::kFrameWindowUpdateSize);
  appender.writeBE<uint32_t>(0);

  parse();
  // This test doesn't ensure that RST_STREAM is generated
  EXPECT_EQ(callbacks_.windowUpdateCalls, 0);
  EXPECT_EQ(callbacks_.streamErrors, 1);
  EXPECT_EQ(callbacks_.lastParseError->getCodecStatusCode(),
      ErrorCode::PROTOCOL_ERROR);
}

TEST_F(HTTP2CodecTest, BasicGoaway) {
  std::unique_ptr<folly::IOBuf> debugData =
    folly::IOBuf::copyBuffer("debugData");
  upstreamCodec_.generateGoaway(output_, 17, ErrorCode::ENHANCE_YOUR_CALM,
                                std::move(debugData));

  parse();
  EXPECT_EQ(callbacks_.goaways, 1);
  EXPECT_EQ(callbacks_.data.move()->moveToFbString(), "debugData");
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BadGoaway) {
  std::unique_ptr<folly::IOBuf> debugData =
    folly::IOBuf::copyBuffer("debugData");
  upstreamCodec_.generateGoaway(output_, 17, ErrorCode::ENHANCE_YOUR_CALM,
                                std::move(debugData));
  EXPECT_DEATH_NO_CORE(upstreamCodec_.generateGoaway(
                         output_, 27, ErrorCode::ENHANCE_YOUR_CALM), ".*");
}

TEST_F(HTTP2CodecTest, DoubleGoaway) {
  parse();
  SetUpUpstreamTest();
  downstreamCodec_.generateGoaway(output_, std::numeric_limits<int32_t>::max(),
                                  ErrorCode::NO_ERROR);
  EXPECT_TRUE(downstreamCodec_.isWaitingToDrain());
  EXPECT_TRUE(downstreamCodec_.isReusable());
  EXPECT_TRUE(downstreamCodec_.isStreamIngressEgressAllowed(0));
  EXPECT_TRUE(downstreamCodec_.isStreamIngressEgressAllowed(1));
  EXPECT_TRUE(downstreamCodec_.isStreamIngressEgressAllowed(2));
  downstreamCodec_.generateGoaway(output_, 0, ErrorCode::NO_ERROR);
  EXPECT_FALSE(downstreamCodec_.isWaitingToDrain());
  EXPECT_FALSE(downstreamCodec_.isReusable());
  EXPECT_TRUE(downstreamCodec_.isStreamIngressEgressAllowed(0));
  EXPECT_FALSE(downstreamCodec_.isStreamIngressEgressAllowed(1));
  EXPECT_TRUE(downstreamCodec_.isStreamIngressEgressAllowed(2));

  EXPECT_TRUE(upstreamCodec_.isStreamIngressEgressAllowed(0));
  EXPECT_TRUE(upstreamCodec_.isStreamIngressEgressAllowed(1));
  EXPECT_TRUE(upstreamCodec_.isStreamIngressEgressAllowed(2));
  parseUpstream();
  EXPECT_TRUE(upstreamCodec_.isStreamIngressEgressAllowed(0));
  EXPECT_FALSE(upstreamCodec_.isStreamIngressEgressAllowed(1));
  EXPECT_TRUE(upstreamCodec_.isStreamIngressEgressAllowed(2));
  EXPECT_EQ(callbacks_.goaways, 2);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);

  upstreamCodec_.generateGoaway(output_, 0, ErrorCode::NO_ERROR);
  EXPECT_TRUE(upstreamCodec_.isStreamIngressEgressAllowed(0));
  EXPECT_FALSE(upstreamCodec_.isStreamIngressEgressAllowed(1));
  EXPECT_FALSE(upstreamCodec_.isStreamIngressEgressAllowed(2));
  parse();
  EXPECT_TRUE(downstreamCodec_.isStreamIngressEgressAllowed(0));
  EXPECT_FALSE(downstreamCodec_.isStreamIngressEgressAllowed(1));
  EXPECT_FALSE(downstreamCodec_.isStreamIngressEgressAllowed(2));
}

TEST_F(HTTP2CodecTest, DoubleGoawayWithError) {
  SetUpUpstreamTest();
  std::unique_ptr<folly::IOBuf> debugData =
    folly::IOBuf::copyBuffer("debugData");
  downstreamCodec_.generateGoaway(output_, std::numeric_limits<int32_t>::max(),
                                  ErrorCode::ENHANCE_YOUR_CALM,
                                  std::move(debugData));
  EXPECT_FALSE(downstreamCodec_.isWaitingToDrain());
  EXPECT_FALSE(downstreamCodec_.isReusable());
  auto ret = downstreamCodec_.generateGoaway(output_, 0,
                                             ErrorCode::NO_ERROR);
  EXPECT_EQ(ret, 0);

  parseUpstream();
  EXPECT_EQ(callbacks_.goaways, 1);
  EXPECT_EQ(callbacks_.data.move()->moveToFbString(), "debugData");
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, GoawayHandling) {
  auto settings = upstreamCodec_.getEgressSettings();
  settings->setSetting(SettingsId::ENABLE_PUSH, 1);
  upstreamCodec_.generateSettings(output_);

  // send request
  HTTPMessage req = getGetRequest();
  HTTPHeaderSize size;
  size.uncompressed = size.compressed = 0;
  upstreamCodec_.generateHeader(output_, 1, req, 0, true, &size);
  EXPECT_GT(size.uncompressed, 0);
  parse();
  callbacks_.expectMessage(true, 1, "/");
  callbacks_.reset();

  SetUpUpstreamTest();
  // drain after this message
  downstreamCodec_.generateGoaway(output_, 1, ErrorCode::NO_ERROR);
  parseUpstream();
  // upstream cannot generate id > 1
  upstreamCodec_.generateHeader(output_, 3, req, 0, false, &size);
  EXPECT_EQ(size.uncompressed, 0);
  upstreamCodec_.generateWindowUpdate(output_, 3, 100);
  upstreamCodec_.generateBody(output_, 3, makeBuf(10), boost::none, false);
  upstreamCodec_.generatePriority(output_, 3,
                                  HTTPMessage::HTTPPriority(0, true, 1));
  upstreamCodec_.generateEOM(output_, 3);
  upstreamCodec_.generateRstStream(output_, 3, ErrorCode::CANCEL);
  EXPECT_EQ(output_.chainLength(), 0);

  // send a push promise that will be rejected by downstream
  req.getHeaders().add("foomonkey", "george");
  downstreamCodec_.generateHeader(output_, 2, req, 1, false, &size);
  EXPECT_GT(size.uncompressed, 0);
  HTTPMessage resp;
  resp.setStatusCode(200);
  // send a push response that will be ignored
  downstreamCodec_.generateHeader(output_, 2, resp, 0, false, &size);
  // window update for push doesn't make any sense, but whatever
  downstreamCodec_.generateWindowUpdate(output_, 2, 100);
  downstreamCodec_.generateBody(output_, 2, makeBuf(10), boost::none, false);
  writeFrameHeaderManual(output_, 20, (uint8_t)http2::FrameType::DATA, 0, 2);
  output_.append(makeBuf(10));

  // tell the upstream no pushing, and parse the first batch
  IOBufQueue dummy;
  upstreamCodec_.generateGoaway(dummy, 0, ErrorCode::NO_ERROR);
  parseUpstream();

  output_.append(makeBuf(10));
  downstreamCodec_.generatePriority(output_, 2,
                                    HTTPMessage::HTTPPriority(0, true, 1));
  downstreamCodec_.generateEOM(output_, 2);
  downstreamCodec_.generateRstStream(output_, 2, ErrorCode::CANCEL);

  // send a response that will be accepted, headers should be ok
  downstreamCodec_.generateHeader(output_, 1, resp, 0, true, &size);
  EXPECT_GT(size.uncompressed, 0);

  // parse the remainder
  parseUpstream();
  callbacks_.expectMessage(true, 1, 200);
}

TEST_F(HTTP2CodecTest, GoawayReply) {
  upstreamCodec_.generateGoaway(output_, 0, ErrorCode::NO_ERROR);

  parse();
  EXPECT_EQ(callbacks_.goaways, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);

  SetUpUpstreamTest();
  HTTPMessage resp;
  resp.setStatusCode(200);
  resp.setStatusMessage("nifty-nice");
  downstreamCodec_.generateHeader(output_, 1, resp, 0);
  downstreamCodec_.generateEOM(output_, 1);
  parseUpstream();
  callbacks_.expectMessage(true, 1, 200);
  EXPECT_TRUE(callbacks_.msg->getHeaders().exists(HTTP_HEADER_DATE));
}

TEST_F(HTTP2CodecTest, BasicSetting) {
  auto settings = upstreamCodec_.getEgressSettings();
  settings->setSetting(SettingsId::MAX_CONCURRENT_STREAMS, 37);
  settings->setSetting(SettingsId::INITIAL_WINDOW_SIZE, 12345);
  upstreamCodec_.generateSettings(output_);

  parse();
  EXPECT_EQ(callbacks_.settings, 1);
  EXPECT_EQ(callbacks_.maxStreams, 37);
  EXPECT_EQ(callbacks_.windowSize, 12345);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, SettingsAck) {
  upstreamCodec_.generateSettingsAck(output_);

  parse();
  EXPECT_EQ(callbacks_.settings, 0);
  EXPECT_EQ(callbacks_.settingsAcks, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BadSettings) {
  auto settings = upstreamCodec_.getEgressSettings();
  settings->setSetting(SettingsId::INITIAL_WINDOW_SIZE, 0xffffffff);
  upstreamCodec_.generateSettings(output_);

  parse();
  EXPECT_EQ(callbacks_.settings, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
}

TEST_F(HTTP2CodecTest, BadPushSettings) {
  auto settings = downstreamCodec_.getEgressSettings();
  settings->clearSettings();
  settings->setSetting(SettingsId::ENABLE_PUSH, 0);
  SetUpUpstreamTest();

  parseUpstream([&] (IOBuf* ingress) {
      EXPECT_EQ(ingress->computeChainDataLength(), http2::kFrameHeaderSize);
    });
  EXPECT_FALSE(upstreamCodec_.supportsPushTransactions());
  // Only way to disable push for downstreamCodec_ is to read
  // ENABLE_PUSH:0 from client
  EXPECT_TRUE(downstreamCodec_.supportsPushTransactions());
  EXPECT_EQ(callbacks_.settings, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}


TEST_F(HTTP2CodecTest, SettingsTableSize) {
  auto settings = upstreamCodec_.getEgressSettings();
  settings->setSetting(SettingsId::HEADER_TABLE_SIZE, 8192);
  upstreamCodec_.generateSettings(output_);

  parse();
  EXPECT_EQ(callbacks_.settings, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);

  callbacks_.reset();

  HTTPMessage resp;
  resp.setStatusCode(200);
  resp.setStatusMessage("nifty-nice");
  resp.getHeaders().add("content-type", "x-coolio");
  SetUpUpstreamTest();
  downstreamCodec_.generateHeader(output_, 1, resp, 0);

  parseUpstream();
  callbacks_.expectMessage(false, 2, 200);
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_TRUE(callbacks_.msg->getHeaders().exists(HTTP_HEADER_DATE));
  EXPECT_EQ("x-coolio", headers.getSingleOrEmpty("content-type"));
}

TEST_F(HTTP2CodecTest, BadSettingsTableSize) {
  auto settings = upstreamCodec_.getEgressSettings();
  settings->setSetting(SettingsId::HEADER_TABLE_SIZE, 8192);
  // This sets the max decoder table size to 8k
  upstreamCodec_.generateSettings(output_);

  parse();
  EXPECT_EQ(callbacks_.settings, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);

  callbacks_.reset();

  // Set max decoder table size back to 4k, but don't parse it.  The
  // upstream encoder will up the table size to 8k per the first settings frame
  // and the HPACK codec will send a code to update the decoder.
  settings->setSetting(SettingsId::HEADER_TABLE_SIZE, 4096);
  upstreamCodec_.generateSettings(output_);

  HTTPMessage resp;
  resp.setStatusCode(200);
  resp.setStatusMessage("nifty-nice");
  resp.getHeaders().add("content-type", "x-coolio");
  SetUpUpstreamTest();
  downstreamCodec_.generateHeader(output_, 1, resp, 0);

  parseUpstream();
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
}

TEST_F(HTTP2CodecTest, BasicPriority) {
  auto pri = HTTPMessage::HTTPPriority(0, true, 1);
  upstreamCodec_.generatePriority(output_, 1, pri);

  EXPECT_TRUE(parse());
  EXPECT_EQ(callbacks_.priority, pri);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BadHeaderPriority) {
  HTTPMessage req = getGetRequest();
  req.setHTTP2Priority(HTTPMessage::HTTPPriority(0, false, 7));
  upstreamCodec_.generateHeader(output_, 1, req, 0, true /* eom */);

  // hack ingress with cirular dep
  EXPECT_TRUE(parse([&] (IOBuf* ingress) {
        folly::io::RWPrivateCursor c(ingress);
        c.skip(http2::kFrameHeaderSize + http2::kConnectionPreface.length());
        c.writeBE<uint32_t>(1);
      }));

  EXPECT_EQ(callbacks_.streamErrors, 1);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BadPriority) {
  auto pri = HTTPMessage::HTTPPriority(0, true, 1);
  upstreamCodec_.generatePriority(output_, 1, pri);

  // hack ingress with cirular dep
  EXPECT_TRUE(parse([&] (IOBuf* ingress) {
        folly::io::RWPrivateCursor c(ingress);
        c.skip(http2::kFrameHeaderSize + http2::kConnectionPreface.length());
        c.writeBE<uint32_t>(1);
      }));

  EXPECT_EQ(callbacks_.streamErrors, 1);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

class DummyQueue: public HTTPCodec::PriorityQueue {
 public:
  DummyQueue() {}
  virtual ~DummyQueue() {}
  virtual void addPriorityNode(
      HTTPCodec::StreamID id,
      HTTPCodec::StreamID) override {
    nodes_.push_back(id);
  }

  std::vector<HTTPCodec::StreamID> nodes_;
};

TEST_F(HTTP2CodecTest, VirtualNodes) {
  DummyQueue queue;
  uint8_t level = 30;
  upstreamCodec_.addPriorityNodes(queue, output_, level);

  EXPECT_TRUE(parse());
  for (int i = 0; i < level; i++) {
    EXPECT_EQ(queue.nodes_[i], upstreamCodec_.mapPriorityToDependency(i));
  }

  // Out-of-range priorites are mapped to the lowest level of virtual nodes.
  EXPECT_EQ(queue.nodes_[level - 1],
            upstreamCodec_.mapPriorityToDependency(level));
  EXPECT_EQ(queue.nodes_[level - 1],
            upstreamCodec_.mapPriorityToDependency(level + 1));
}

TEST_F(HTTP2CodecTest, BasicPushPromise) {
  upstreamCodec_.generateSettings(output_);
  parse();
  EXPECT_FALSE(upstreamCodec_.supportsPushTransactions());
  EXPECT_FALSE(downstreamCodec_.supportsPushTransactions());

  auto settings = upstreamCodec_.getEgressSettings();
  settings->setSetting(SettingsId::ENABLE_PUSH, 1);
  upstreamCodec_.generateSettings(output_);
  parse();
  EXPECT_TRUE(upstreamCodec_.supportsPushTransactions());
  EXPECT_TRUE(downstreamCodec_.supportsPushTransactions());

  SetUpUpstreamTest();

  HTTPCodec::StreamID assocStream = 7;
  for (auto i = 0; i < 2; i++) {
    // Push promise
    HTTPCodec::StreamID pushStream = downstreamCodec_.createStream();
    HTTPMessage req = getGetRequest();
    req.getHeaders().add("user-agent", "coolio");
    downstreamCodec_.generateHeader(output_, pushStream, req, assocStream);

    parseUpstream();
    callbacks_.expectMessage(false, 2, "/"); // + host
    EXPECT_EQ(callbacks_.assocStreamId, assocStream);
    EXPECT_EQ(callbacks_.headersCompleteId, pushStream);
    auto& headers = callbacks_.msg->getHeaders();
    EXPECT_EQ("coolio", headers.getSingleOrEmpty("user-agent"));
    callbacks_.reset();

    // Actual reply headers
    HTTPMessage resp;
    resp.setStatusCode(200);
    resp.getHeaders().add("content-type", "text/plain");
    downstreamCodec_.generateHeader(output_, pushStream, resp, 0);

    parseUpstream();
    callbacks_.expectMessage(false, 2, 200);
    EXPECT_EQ(callbacks_.headersCompleteId, pushStream);
    EXPECT_EQ(callbacks_.assocStreamId, 0);
    EXPECT_TRUE(callbacks_.msg->getHeaders().exists(HTTP_HEADER_DATE));
    EXPECT_EQ("text/plain",
              callbacks_.msg->getHeaders().getSingleOrEmpty("content-type"));
    callbacks_.reset();
  }
}

TEST_F(HTTP2CodecTest, BadPushPromise) {
  // ENABLE_PUSH is now 0 by default
  SetUpUpstreamTest();
  HTTPMessage req = getGetRequest();
  req.getHeaders().add("user-agent", "coolio");
  downstreamCodec_.generateHeader(output_, 2, req, 1);

  parseUpstream();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.assocStreamId, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
}

TEST_F(HTTP2CodecTest, BadServerPreface) {
  output_.move();
  downstreamCodec_.generateWindowUpdate(output_, 0, 10);
  parseUpstream();
  EXPECT_EQ(callbacks_.messageBegin, 0);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.assocStreamId, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
}

/**
 * Mostly ripped from HTTP2Codec::generateHeader.  This writes out frames
 * like Chrome does, breaking on 1024 - frame header, with some of the not
 * needed cases removed.  It's specialized to write only a single continuation
 * frame with optionally malformed length
 */
void generateHeaderChrome(HPACKCodec& headerCodec,
                          folly::IOBufQueue& writeBuf,
                          HTTPCodec::StreamID stream,
                          const HTTPMessage& msg,
                          HTTPCodec::StreamID assocStream,
                          bool eom,
                          HTTPHeaderSize* size,
                          bool malformed) {
  VLOG(4) << "generating " << ((assocStream != 0) ? "PUSH_PROMISE" : "HEADERS")
          << " for stream=" << stream;
  std::vector<proxygen::compress::Header> allHeaders;

  const string& method = msg.getMethodString();
  const string& scheme = (msg.isSecure() ? http2::kHttps : http2::kHttp);
  const string& path = msg.getURL();
  const HTTPHeaders& headers = msg.getHeaders();
  const string& host = headers.getSingleOrEmpty(HTTP_HEADER_HOST);
  allHeaders.emplace_back(http2::kMethod, method);
  allHeaders.emplace_back(http2::kScheme, scheme);
  allHeaders.emplace_back(http2::kPath, path);
  if (!host.empty()) {
    allHeaders.emplace_back(http2::kAuthority, host);
  }

  // Add the HTTP headers supplied by the caller, but skip
  // any per-hop headers that aren't supported in HTTP/2.
  msg.getHeaders().forEachWithCode(
    [&] (HTTPHeaderCode code,
         const string& name,
         const string& value) {

      // Note this code will not drop headers named by Connection.  That's the
      // caller's job

      // see HTTP/2 spec, 8.1.2
      DCHECK(name != "TE" || value == "trailers");
      if ((name.size() > 0 && name[0] != ':') &&
          code != HTTP_HEADER_HOST) {
        allHeaders.emplace_back(code, name, value);
      }
    });

  headerCodec.setEncodeHeadroom(http2::kFrameHeadersBaseMaxSize);
  auto out = headerCodec.encode(allHeaders);
  if (size) {
    *size = headerCodec.getEncodedSize();
  }

  IOBufQueue queue(IOBufQueue::cacheChainLength());
  queue.append(std::move(out));
  if (queue.chainLength() > 0) {

    auto chunk = queue.split(std::min((size_t)(1024 - 14),
                                      queue.chainLength()));

    bool endHeaders = queue.chainLength() == 0;
    CHECK_EQ(assocStream, 0);
    http2::writeHeaders(writeBuf,
                        std::move(chunk),
                        stream,
                        http2::DefaultPriority,
                        http2::kNoPadding,
                        eom,
                        endHeaders);
    while (!endHeaders) {
      CHECK_EQ(queue.chainLength(), 1015);
      chunk = queue.split(std::min(size_t(1024 - http2::kFrameHeaderSize),
                                   queue.chainLength()));
      endHeaders = queue.chainLength() == 0;
      CHECK(endHeaders);
      VLOG(4) << "generating CONTINUATION for stream=" << stream;
      writeFrameHeaderManual(writeBuf,
                             malformed ? 1024 : chunk->computeChainDataLength(),
                             9,
                             http2::END_HEADERS,
                             stream);
      writeBuf.append(std::move(chunk));
    }
  }
}

class ChromeHTTP2Test : public HTTP2CodecTest,
                 public ::testing::WithParamInterface<string> {
};


const string agent1("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_5) "
                    "AppleWebKit/537.36 (KHTML, like Gecko) "
                    "Chrome/42.0.2311.11 Safari/537.36");
const string agent2("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_5) "
                    "AppleWebKit/537.36 (KHTML, like Gecko) "
                    "Chrome/43.0.2311.11 Safari/537.36");
const string agent3("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                    "AppleWebKit/537.36 (KHTML, like Gecko) "
                    "Chrome/42.0.2311.135 Safari/537.36 Edge/12.10240");

INSTANTIATE_TEST_CASE_P(AgentTest,
                        ChromeHTTP2Test,
                        ::testing::Values(agent1, agent2));

TEST_F(HTTP2CodecTest, Normal1024Continuation) {
  HTTPMessage req = getGetRequest();
  string bigval(8691, '!');
  bigval.append(8691, ' ');
  req.getHeaders().add("x-headr", bigval);
  req.setHTTP2Priority(HTTPMessage::HTTPPriority(0, false, 7));
  upstreamCodec_.generateHeader(output_, 1, req, 0);

  parse();
  callbacks_.expectMessage(false, -1, "/");
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_EQ(bigval, headers.getSingleOrEmpty("x-headr"));
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.headersComplete, 1);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);

  upstreamCodec_.generateSettingsAck(output_);
  parse();
  EXPECT_EQ(callbacks_.settingsAcks, 1);
}

TEST_F(HTTP2CodecTest, Chrome16kb) {
  HTTPMessage req = getGetRequest();
  string bigval(8691, '!');
  bigval.append(8691, ' ');
  req.getHeaders().add("x-headr", bigval);
  req.getHeaders().add("user-agent", agent2);
  upstreamCodec_.generateHeader(output_, 1, req, 0);
  upstreamCodec_.generateRstStream(output_, 1, ErrorCode::PROTOCOL_ERROR);

  parse();
  callbacks_.expectMessage(false, -1, "/");
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_EQ(bigval, headers.getSingleOrEmpty("x-headr"));
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.headersComplete, 1);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
  EXPECT_EQ(callbacks_.aborts, 1);
  EXPECT_EQ(callbacks_.lastErrorCode, ErrorCode::NO_ERROR);

  upstreamCodec_.generateSettingsAck(output_);
  parse();
  EXPECT_EQ(callbacks_.settingsAcks, 1);
}

TEST_F(HTTP2CodecTest, StreamIdOverflow) {
  HTTP2Codec codec(TransportDirection::UPSTREAM);

  HTTPCodec::StreamID streamId;
  codec.setNextEgressStreamId(std::numeric_limits<int32_t>::max() - 10);
  while (codec.isReusable()) {
    streamId = codec.createStream();
  }
  EXPECT_EQ(streamId, std::numeric_limits<int32_t>::max() - 2);
}

TEST_F(HTTP2CodecTest, TestMultipleDifferentContentLengthHeaders) {
  // Generate a POST request with two Content-Length headers
  // NOTE: getPostRequest already adds the content-length
  HTTPMessage req = getPostRequest();
  req.getHeaders().add("content-length", "300");
  EXPECT_EQ(req.getHeaders().getNumberOfValues("content-length"), 2);

  upstreamCodec_.generateHeader(output_, 1, req, 0, true /* eom */);
  parse();

  // Check that the request fails before the codec finishes parsing the headers
  EXPECT_EQ(callbacks_.streamErrors, 1);
  EXPECT_EQ(callbacks_.headersComplete, 0);
  EXPECT_EQ(callbacks_.lastParseError->getHttpStatusCode(), 400);
}

TEST_F(HTTP2CodecTest, TestMultipleIdenticalContentLengthHeaders) {
  // Generate a POST request with two Content-Length headers
  // NOTE: getPostRequest already adds the content-length
  HTTPMessage req = getPostRequest();
  req.getHeaders().add("content-length", "200");
  EXPECT_EQ(req.getHeaders().getNumberOfValues("content-length"), 2);

  upstreamCodec_.generateHeader(output_, 1, req, 0, true /* eom */);
  parse();

  // Check that the headers parsing completes correctly
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.headersComplete, 1);
}

TEST_F(HTTP2CodecTest, CleartextUpgrade) {
  HTTPMessage req = getGetRequest("/guacamole");
  req.getHeaders().add("user-agent", "coolio");
  HTTP2Codec::requestUpgrade(req);
  EXPECT_EQ(req.getHeaders().getSingleOrEmpty(HTTP_HEADER_UPGRADE), "h2c");
  EXPECT_TRUE(req.checkForHeaderToken(HTTP_HEADER_CONNECTION,
                                      "Upgrade", false));
  EXPECT_TRUE(req.checkForHeaderToken(
                HTTP_HEADER_CONNECTION,
                http2::kProtocolSettingsHeader.c_str(), false));
  EXPECT_GT(
    req.getHeaders().getSingleOrEmpty(http2::kProtocolSettingsHeader).length(),
    0);
}

TEST_F(HTTP2CodecTest, HTTP2SettingsSuccess) {
  HTTPMessage req = getGetRequest("/guacamole");

  // empty settings
  req.getHeaders().add(http2::kProtocolSettingsHeader, "");
  EXPECT_TRUE(downstreamCodec_.onIngressUpgradeMessage(req));

  // real settings (overwrites empty)
  HTTP2Codec::requestUpgrade(req);
  EXPECT_TRUE(downstreamCodec_.onIngressUpgradeMessage(req));
}

TEST_F(HTTP2CodecTest, HTTP2SettingsFailure) {
  HTTPMessage req = getGetRequest("/guacamole");
  // no settings
  EXPECT_FALSE(downstreamCodec_.onIngressUpgradeMessage(req));

  HTTPHeaders& headers = req.getHeaders();

  // Not base64_url settings
  headers.set(http2::kProtocolSettingsHeader, "????");
  EXPECT_FALSE(downstreamCodec_.onIngressUpgradeMessage(req));
  headers.set(http2::kProtocolSettingsHeader, "AAA");
  EXPECT_FALSE(downstreamCodec_.onIngressUpgradeMessage(req));

  // Too big
  string bigSettings((http2::kMaxFramePayloadLength + 1) * 4 / 3, 'A');
  headers.set(http2::kProtocolSettingsHeader, bigSettings);
  EXPECT_FALSE(downstreamCodec_.onIngressUpgradeMessage(req));

  // Malformed (not a multiple of 6)
  headers.set(http2::kProtocolSettingsHeader, "AAAA");
  EXPECT_FALSE(downstreamCodec_.onIngressUpgradeMessage(req));

  // Two headers
  headers.set(http2::kProtocolSettingsHeader, "AAAAAAAA");
  headers.add(http2::kProtocolSettingsHeader, "AAAAAAAA");
  EXPECT_FALSE(downstreamCodec_.onIngressUpgradeMessage(req));
}
