/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/Cursor.h>
#include <proxygen/lib/http/codec/experimental/HTTP2Codec.h>
#include <proxygen/lib/http/codec/experimental/test/HTTP2FramerTest.h>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <proxygen/lib/utils/Logging.h>

#include <gtest/gtest.h>
#include <random>

using namespace proxygen;
using namespace folly;
using namespace folly::io;
using namespace std;

class HTTP2CodecTest : public testing::Test {
 public:
  void SetUp() override {
    HTTP2Codec::setHeaderSplitSize(http2::kMaxFramePayloadLengthMin);
    downstreamCodec_.setCallback(&callbacks_);
    upstreamCodec_.setCallback(&callbacks_);
    // Most tests are downstream tests, so generate the upstream conn preface
    // by default
    upstreamCodec_.generateConnectionPreface(output_);
  }

  void SetUpUpstreamTest() {
    output_.move();
    downstreamCodec_.generateConnectionPreface(output_); // no-op
    downstreamCodec_.generateSettings(output_);
  }

  bool parse(std::function<void(IOBuf*)> hackIngress =
             std::function<void(IOBuf*)>()) {
    return parseImpl(downstreamCodec_, hackIngress);
  }

  bool parseUpstream(std::function<void(IOBuf*)> hackIngress =
                     std::function<void(IOBuf*)>()) {
    return parseImpl(upstreamCodec_, hackIngress);
  }

  /*
   * hackIngress is used to keep the codec's strict checks while having
   * separate checks for tests
   */
  bool parseImpl(HTTP2Codec& codec, std::function<void(IOBuf*)> hackIngress) {
    dumpToFile(codec.getTransportDirection() == TransportDirection::UPSTREAM);
    auto ingress = output_.move();
    if (hackIngress) {
      hackIngress(ingress.get());
    }
    size_t parsed = codec.onIngress(*ingress);
    return (parsed == ingress->computeChainDataLength());
  }

  /*
   * dumpToFile dumps binary frames to files ("/tmp/http2_*.bin"),
   * allowing debugging individual frames, e.g., used by ti/tools/spdyprint
   * @note: assign true to dump_ to turn on dumpToFile
   */
  void dumpToFile(bool isUpstream=false) {
    if (!dump_) {
      return;
    }
    auto endpoint = isUpstream ? "client" : "server";
    auto filename = folly::to<std::string>(
        "/tmp/http2_", endpoint, "_", testInfo_->name(), ".bin");
    dumpBinToFile(filename, output_.front());
  }

  void testBigHeader(bool continuation);


 protected:
  FakeHTTPCodecCallback callbacks_;
  HTTP2Codec upstreamCodec_{TransportDirection::UPSTREAM};
  HTTP2Codec downstreamCodec_{TransportDirection::DOWNSTREAM};
  IOBufQueue output_{IOBufQueue::cacheChainLength()};
  const testing::TestInfo*
    testInfo_{testing::UnitTest::GetInstance()->current_test_info()};
  bool dump_{false};
};

TEST_F(HTTP2CodecTest, BasicHeader) {
  HTTPMessage req = getGetRequest("/guacamole");
  req.getHeaders().add("user-agent", "coolio");
  // Connection header will get dropped
  req.getHeaders().add("Connection", "Love");
  req.setSecure(true);
  upstreamCodec_.generateHeader(output_, 1, req, 0, true /* eom */);

  parse();
  callbacks_.expectMessage(true, 2, "/guacamole");
  EXPECT_TRUE(callbacks_.msg->isSecure());
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_EQ("coolio", headers.getSingleOrEmpty("user-agent"));
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

  HPACKCodec09 headerCodec(TransportDirection::UPSTREAM);
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

  HPACKCodec09 headerCodec(TransportDirection::UPSTREAM);
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

  HPACKCodec09 headerCodec(TransportDirection::UPSTREAM);
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
  callbacks_.expectMessage(true, 1, 200);
  const auto& headers = callbacks_.msg->getHeaders();
  // HTTP/2 doesnt support serialization - instead you get the default
  EXPECT_EQ("OK", callbacks_.msg->getStatusMessage());
  EXPECT_EQ("x-coolio", headers.getSingleOrEmpty("content-type"));
}

TEST_F(HTTP2CodecTest, BadHeadersReply) {
  static const std::string v1("200");
  static const vector<proxygen::compress::Header> respHeaders = {
    { http2::kStatus, v1 },
  };

  HPACKCodec09 headerCodec(TransportDirection::DOWNSTREAM);
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
  upstreamCodec_.generateBody(output_, 1, std::move(buf->clone()),
                              HTTPCodec::NoPadding, true);

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
  upstreamCodec_.generateGoaway(output_, 17, ErrorCode::ENHANCE_YOUR_CALM);

  parse();
  EXPECT_EQ(callbacks_.goaways, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BadGoaway) {
  upstreamCodec_.generateGoaway(output_, 17, ErrorCode::ENHANCE_YOUR_CALM);
  EXPECT_DEATH_NO_CORE(upstreamCodec_.generateGoaway(
                         output_, 27, ErrorCode::ENHANCE_YOUR_CALM), ".*");
}

TEST_F(HTTP2CodecTest, DoubleGoaway) {
  SetUpUpstreamTest();
  downstreamCodec_.generateGoaway(output_, std::numeric_limits<int32_t>::max(),
                                  ErrorCode::NO_ERROR);
  EXPECT_TRUE(downstreamCodec_.isWaitingToDrain());
  EXPECT_TRUE(downstreamCodec_.isReusable());
  downstreamCodec_.generateGoaway(output_, 0,
                                  ErrorCode::NO_ERROR);
  EXPECT_FALSE(downstreamCodec_.isWaitingToDrain());
  EXPECT_FALSE(downstreamCodec_.isReusable());

  parseUpstream();
  EXPECT_EQ(callbacks_.goaways, 2);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, DoubleGoawayWithError) {
  SetUpUpstreamTest();
  downstreamCodec_.generateGoaway(output_, std::numeric_limits<int32_t>::max(),
                                  ErrorCode::ENHANCE_YOUR_CALM);
  EXPECT_FALSE(downstreamCodec_.isWaitingToDrain());
  EXPECT_FALSE(downstreamCodec_.isReusable());
  auto ret = downstreamCodec_.generateGoaway(output_, 0,
                                             ErrorCode::NO_ERROR);
  EXPECT_EQ(ret, 0);

  parseUpstream();
  EXPECT_EQ(callbacks_.goaways, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
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
      // set ENABLE_PUSH to 1
      folly::io::RWPrivateCursor c(ingress);
      c.skip(http2::kFrameHeaderSize + sizeof(uint16_t));
      c.writeBE<uint32_t>(1);
    });
  EXPECT_EQ(callbacks_.settings, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
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
  callbacks_.expectMessage(false, 1, 200);
  const auto& headers = callbacks_.msg->getHeaders();
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
  http2::writePriority(output_, 1, {0, true, 1});

  EXPECT_TRUE(parse());
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HTTP2CodecTest, BasicPushPromise) {
  auto settings = upstreamCodec_.getEgressSettings();
  settings->setSetting(SettingsId::ENABLE_PUSH, 1);
  SetUpUpstreamTest();
  HTTPMessage req = getGetRequest();
  req.getHeaders().add("user-agent", "coolio");
  downstreamCodec_.generateHeader(output_, 2, req, 1);

  parseUpstream();
  callbacks_.expectMessage(false, 2, "/"); // + host
  EXPECT_EQ(callbacks_.assocStreamId, 1);
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_EQ("coolio", headers.getSingleOrEmpty("user-agent"));
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
void generateHeaderChrome(HPACKCodec09& headerCodec,
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
    CHECK(assocStream == 0);
    http2::writeHeaders(writeBuf,
                        std::move(chunk),
                        stream,
                        http2::PriorityUpdate({0, false, 16}),
                        http2::kNoPadding,
                        eom,
                        endHeaders);
    while (!endHeaders) {
      CHECK(queue.chainLength() == 1015);
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
                    "Chrome/43.0.2311.11 Safari/537.36");;

// Chrome < 43 can generate malformed CONTINUATION frames
TEST_P(ChromeHTTP2Test, ChromeContinuation) {
  HPACKCodec09 headerCodec(TransportDirection::UPSTREAM);
  HTTPMessage req = getGetRequest();
  string agent = GetParam();
  req.getHeaders().add("user-agent", agent);
  string bigval(954, '!');
  bigval.append(954, ' ');
  req.getHeaders().add("x-header", bigval);
  generateHeaderChrome(headerCodec, output_, 1, req, 0, false, nullptr,
                       agent.find("Chrome/43") == string::npos);

  parse();
  callbacks_.expectMessage(false, -1, "/");
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_EQ(agent, headers.getSingleOrEmpty("user-agent"));
  EXPECT_EQ(bigval, headers.getSingleOrEmpty("x-header"));
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.headersComplete, 1);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);

  upstreamCodec_.generateSettingsAck(output_);
  parse();
  EXPECT_EQ(callbacks_.settingsAcks, 1);
}

TEST_P(ChromeHTTP2Test, ChromeContinuationSecondStream) {
  HPACKCodec09 headerCodec(TransportDirection::UPSTREAM);
  HTTPMessage req = getGetRequest();
  string agent = GetParam();
  req.getHeaders().add("user-agent", agent);
  generateHeaderChrome(headerCodec, output_, 1, req, 0, false, nullptr,
                       false);
  string bigval(1004, '!');
  bigval.append(1004, ' ');
  req.getHeaders().add("x-headerx", bigval);
  generateHeaderChrome(headerCodec, output_, 3, req, 0, false, nullptr,
                       agent.find("Chrome/43") == string::npos);

  parse();
  const auto& headers = callbacks_.msg->getHeaders();
  EXPECT_EQ(agent, headers.getSingleOrEmpty("user-agent"));
  EXPECT_EQ(bigval, headers.getSingleOrEmpty("x-headerx"));
  EXPECT_EQ(callbacks_.messageBegin, 2);
  EXPECT_EQ(callbacks_.headersComplete, 2);
  EXPECT_EQ(callbacks_.messageComplete, 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);

  upstreamCodec_.generateSettingsAck(output_);
  parse();
  EXPECT_EQ(callbacks_.settingsAcks, 1);
}

INSTANTIATE_TEST_CASE_P(AgentTest,
                        ChromeHTTP2Test,
                        ::testing::Values(agent1, agent2));

TEST_F(HTTP2CodecTest, Normal1024Continuation) {
  HPACKCodec09 headerCodec(TransportDirection::UPSTREAM);
  HTTPMessage req = getGetRequest();
  string bigval(8691, '!');
  bigval.append(8691, ' ');
  req.getHeaders().add("x-headr", bigval);
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
