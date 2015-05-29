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
#include <proxygen/lib/http/codec/SPDYCodec.h>
#include <proxygen/lib/http/codec/SPDYConstants.h>
#include <proxygen/lib/http/codec/SPDYVersionSettings.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <random>

using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;

size_t parseSPDY(SPDYCodec* codec, const uint8_t* inputData, uint32_t length,
                 int32_t atOnce = 0) {
  return parse(codec, inputData, length, atOnce);
}

uint8_t shortSynStream[] =
{ 0x80, 0x02, 0x00, 0x01,
  0x01, 0x00, 0x00, 0x04,  // length must be >= 12
  0x61, 0x62, 0x63, 0x64
};

TEST(SPDYCodecTest, JunkSPDY) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY2);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  size_t unconsumed =
    parseSPDY(&codec, shortSynStream, sizeof(shortSynStream), -1);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 0);
}

uint8_t longNoop[] =
{ 0x80, 0x02, 0x00, 0x05,
  0x00, 0x00, 0x00, 0x04,
  0x00, 0x00, 0x00, 0x00
};

TEST(SPDYCodecTest, LongNoop) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY2);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  size_t unconsumed =
    parseSPDY(&codec, longNoop, sizeof(longNoop), -1);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 0);
}

uint8_t longPing[] =
{ 0x80, 0x02, 0x00, 0x06,
  0x00, 0x00, 0x00, 0x05,
  0x00, 0x00, 0x00, 0x00,
  0x00
};

TEST(SPDYCodecTest, LongPing) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY2);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  size_t unconsumed =
    parseSPDY(&codec, longPing, sizeof(longPing), -1);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 0);
}

uint8_t badType[] =
{ 0x80, 0x02, 0x00, 0x0A,
  0x00, 0x00, 0x00, 0x05,
  0x00, 0x00, 0x00, 0x00,
  0x00
};

TEST(SPDYCodecTest, BadType) {
  // If an endpoint receives a control frame for a type it does not recognize,
  // it must ignore the frame.
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY2);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  size_t unconsumed =
    parseSPDY(&codec, badType, sizeof(badType), -1);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
}

/**
 * A request from firefox for facebook.com
 */
uint8_t synStream[] =
{ 0x80, 0x02, 0x00, 0x01, 0x01, 0x00, 0x02, 0x38,
  0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
  0x40, 0x00, 0x78, 0xbb, 0xdf, 0xa2, 0x51, 0xb2,
  0x74, 0x54, 0x5d, 0x6f, 0xda, 0x30, 0x14, 0xf5,
  0xd6, 0x8d, 0x7d, 0x68, 0xea, 0xfe, 0x82, 0x5f,
  0x2a, 0x6d, 0x1d, 0x0d, 0x89, 0x13, 0x3b, 0x4e,
  0xa3, 0x68, 0x0a, 0x60, 0xa3, 0x75, 0xd0, 0x0d,
  0xda, 0x42, 0xfb, 0x14, 0x85, 0x10, 0x0a, 0x85,
  0x24, 0x34, 0x5f, 0xcd, 0xfa, 0xeb, 0x77, 0x53,
  0x3a, 0xa6, 0xb5, 0xda, 0x83, 0xa3, 0xe8, 0xde,
  0x73, 0x6d, 0x1f, 0xdf, 0x7b, 0x0e, 0x7a, 0x87,
  0x1a, 0x11, 0xe8, 0x30, 0x99, 0xa1, 0xbd, 0x9e,
  0x38, 0x47, 0x8d, 0x0c, 0x5a, 0x17, 0x85, 0xe8,
  0xf5, 0x22, 0xcf, 0x37, 0x19, 0xda, 0xab, 0x31,
  0x2f, 0x5a, 0xe8, 0xcd, 0x63, 0x09, 0x7a, 0xfb,
  0x67, 0x27, 0xf4, 0xaa, 0x16, 0x17, 0xfa, 0x30,
  0x07, 0xbd, 0x4e, 0xe1, 0xf5, 0x94, 0x20, 0x89,
  0x50, 0x63, 0x3b, 0x7a, 0xe8, 0xeb, 0x8e, 0x6c,
  0xf3, 0x19, 0x87, 0x2f, 0xd5, 0xd3, 0x68, 0xb4,
  0xb6, 0x6f, 0x1d, 0x55, 0xb1, 0x9a, 0x87, 0xad,
  0xc3, 0x87, 0x3f, 0x8e, 0x3e, 0x3e, 0xd1, 0x37,
  0xda, 0x0f, 0xe3, 0xa3, 0x22, 0x6b, 0x86, 0xf1,
  0x03, 0x80, 0xa2, 0xc6, 0xb6, 0x63, 0x2f, 0xf7,
  0x61, 0xa8, 0x52, 0x67, 0x36, 0x15, 0xea, 0x79,
  0x99, 0x16, 0xf3, 0x8c, 0x26, 0xd1, 0x25, 0xbd,
  0x59, 0x8e, 0x16, 0xa9, 0x91, 0x9e, 0x47, 0x36,
  0x5e, 0x17, 0xce, 0xf5, 0x70, 0x7c, 0x31, 0x6d,
  0xf7, 0x46, 0xb9, 0x5f, 0x9c, 0x58, 0xc3, 0x6f,
  0x37, 0xbd, 0xa4, 0x7f, 0x25, 0x4a, 0xd7, 0xb5,
  0x71, 0xe0, 0xd5, 0x7a, 0x75, 0x18, 0xd7, 0x0c,
  0x93, 0x71, 0x6e, 0x40, 0x24, 0x8b, 0x1c, 0x62,
  0xe3, 0x2a, 0x73, 0xe8, 0x81, 0xee, 0x26, 0xbf,
  0x4a, 0x2f, 0xf6, 0xab, 0x76, 0x20, 0x82, 0x55,
  0xe2, 0x42, 0x80, 0xc0, 0xd2, 0x74, 0x5d, 0xa3,
  0x84, 0x9b, 0xdc, 0xb2, 0xf1, 0x3c, 0x75, 0xd4,
  0xfe, 0x70, 0x12, 0x67, 0xc3, 0xc1, 0xe9, 0x77,
  0x19, 0x8f, 0xaf, 0x57, 0xaa, 0xae, 0xb8, 0x93,
  0x71, 0xf7, 0x6c, 0x32, 0x4b, 0xa2, 0x7c, 0x9c,
  0xaf, 0x7e, 0x14, 0xc1, 0x26, 0x1c, 0xf1, 0x52,
  0x16, 0x83, 0xbc, 0xed, 0x2a, 0xed, 0x9f, 0xe4,
  0xaa, 0xbc, 0x53, 0x6e, 0x5c, 0x40, 0x5d, 0x2e,
  0xd4, 0x95, 0x28, 0x6d, 0x9c, 0x39, 0xae, 0xcf,
  0x4e, 0xce, 0x46, 0x13, 0xcb, 0x94, 0x62, 0x35,
  0xbc, 0x4f, 0x4a, 0x80, 0x99, 0x47, 0xb7, 0x47,
  0x36, 0xf6, 0x83, 0xdc, 0xd1, 0x74, 0xb0, 0x01,
  0x93, 0xe9, 0xdc, 0xb2, 0x88, 0x76, 0x40, 0x24,
  0x83, 0x3b, 0xa8, 0x36, 0xde, 0x38, 0xd4, 0x84,
  0x2f, 0x0c, 0x33, 0x48, 0x28, 0x74, 0xc4, 0x60,
  0x0b, 0x63, 0x06, 0x17, 0x35, 0x29, 0xe9, 0x92,
  0x1d, 0x2d, 0x97, 0x88, 0xba, 0xef, 0xa1, 0xec,
  0x66, 0x53, 0x22, 0xb7, 0xfb, 0x51, 0xc6, 0x2d,
  0x5d, 0x67, 0x22, 0x27, 0xd2, 0xa3, 0xd3, 0xee,
  0x12, 0xf0, 0x75, 0x99, 0xa7, 0xfb, 0x50, 0x04,
  0xbe, 0xa4, 0xea, 0x8c, 0x42, 0x5d, 0x2a, 0xb5,
  0x8e, 0x1b, 0xfc, 0x93, 0x06, 0xf3, 0x61, 0x0c,
  0xdc, 0xcc, 0x7c, 0x4c, 0x7b, 0x74, 0x26, 0xd6,
  0x11, 0xf9, 0x0f, 0xa2, 0x08, 0xf2, 0x67, 0x47,
  0xd6, 0x97, 0x5b, 0x27, 0xfe, 0x0c, 0xd2, 0xf9,
  0x9d, 0x34, 0x09, 0xd1, 0x75, 0x6e, 0x18, 0x4c,
  0xf8, 0xf9, 0x23, 0x92, 0x51, 0x58, 0x5c, 0xed,
  0xed, 0x28, 0xe9, 0x9a, 0xd1, 0x11, 0xc1, 0x42,
  0x76, 0x37, 0x1e, 0x9d, 0xef, 0x78, 0x49, 0xc6,
  0x49, 0xa7, 0x63, 0xe3, 0xbb, 0x19, 0x3c, 0x92,
  0xa5, 0x55, 0xcc, 0x62, 0xe8, 0xfd, 0x5f, 0x07,
  0x46, 0xa7, 0x83, 0xe4, 0x7e, 0xb9, 0x5e, 0xfb,
  0x2d, 0xaa, 0xa8, 0xf8, 0xd3, 0xa5, 0xa6, 0xd9,
  0xf8, 0x62, 0x5a, 0xc4, 0x79, 0x61, 0xe3, 0xfe,
  0x32, 0x2e, 0x2a, 0x5c, 0x71, 0xe6, 0x31, 0xe8,
  0x7a, 0x5a, 0x1e, 0x6b, 0x86, 0xa2, 0x7e, 0xc6,
  0xbd, 0x10, 0x1a, 0xdd, 0x02, 0xc7, 0x7e, 0x30,
  0x6f, 0x2c, 0xc1, 0x2b, 0xe6, 0x49, 0xd5, 0xaa,
  0x93, 0x8a, 0xf6, 0x1b, 0x00, 0x00, 0xff, 0xff
};

TEST(SPDYCodecTest, SynStreamBoundaries) {
  for (int i = -1; i < int(sizeof(synStream)); i++) {
    SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY2);
    FakeHTTPCodecCallback callbacks;
    codec.setCallback(&callbacks);
    size_t unconsumed =
      parseSPDY(&codec, synStream, sizeof(synStream), i);
    EXPECT_EQ(unconsumed, 0);
    EXPECT_EQ(callbacks.messageBegin, 1);
    EXPECT_EQ(callbacks.headersComplete, 1);
    EXPECT_EQ(callbacks.messageComplete, 1);
    EXPECT_EQ(callbacks.streamErrors, 0);
    EXPECT_EQ(callbacks.sessionErrors, 0);
  }
}

TEST(SPDYCodecTest, SetSettings) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3);
  HTTPSettings* settings = codec.getEgressSettings();
  // There are 2 settings by default.  Turn on another setting
  settings->setSetting(SettingsId::_SPDY_DOWNLOAD_BANDWIDTH, 10);
  // This should no-op since this setting should be off by default
  settings->unsetSetting(SettingsId::_SPDY_ROUND_TRIP_TIME);
  EXPECT_EQ(settings->getSetting(SettingsId::_SPDY_ROUND_TRIP_TIME), nullptr);
  EXPECT_EQ(settings->getSetting(SettingsId::_SPDY_CURRENT_CWND), nullptr);
  EXPECT_EQ(settings->getSetting(SettingsId::MAX_CONCURRENT_STREAMS)->value,
            spdy::kMaxConcurrentStreams);
  EXPECT_EQ(settings->getSetting(SettingsId::INITIAL_WINDOW_SIZE)->value,
            spdy::kInitialWindow);
  EXPECT_EQ(
    settings->getSetting(SettingsId::_SPDY_DOWNLOAD_BANDWIDTH)->value, 10);
  // Turn off one of the defaults
  settings->unsetSetting(SettingsId::MAX_CONCURRENT_STREAMS);
  // Change the value of an existing default setting
  settings->setSetting(SettingsId::INITIAL_WINDOW_SIZE, 123);
  EXPECT_EQ(settings->getSetting(SettingsId::MAX_CONCURRENT_STREAMS),
            nullptr);
  EXPECT_EQ(settings->getSetting(SettingsId::INITIAL_WINDOW_SIZE)->value,
            123);
  EXPECT_EQ(settings->getNumSettings(), 2);
  // Change the value of a unset setting
  settings->setSetting(SettingsId::MAX_CONCURRENT_STREAMS, 400);
  EXPECT_EQ(settings->getSetting(SettingsId::MAX_CONCURRENT_STREAMS)->value,
            400);
  EXPECT_EQ(settings->getNumSettings(), 3);
}

TEST(SPDYCodecTest, FrameTooLarge) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY2);
  codec.setMaxFrameLength(500);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  size_t unconsumed = parseSPDY(&codec, synStream, sizeof(synStream), -1);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 1);
}

TEST(SPDYCodecTest, FrameUncompressedTooLarge) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY2);
  codec.setMaxUncompressedHeaders(600);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  size_t unconsumed = parseSPDY(&codec, synStream, sizeof(synStream), -1);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 1);
}

uint8_t spdy3UnknownCtlFrame[] =
{ 0x80, 0x03, 0x00, 0x0B, // ctl frame for spdy/3 (11 is not defined)
  0x00, 0x00, 0x00, 0x02, // len = 2
  0xD4, 0x74 // The data
};

TEST(SPDYCodecTest, UnsupportedVersion) {
  // Send a spdy/2 frame on a spdy/3 codec
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  size_t unconsumed = parseSPDY(&codec, shortSynStream,
                                sizeof(shortSynStream), -1);
  EXPECT_EQ(unconsumed, 0);
  // Expect a GOAWAY with PROTOCOL_ERROR (because of unsupported version)
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.lastParseError->getCodecStatusCode(),
            spdy::rstToErrorCode(spdy::RST_PROTOCOL_ERROR));
}

TEST(SPDYCodecTest, UnsupportedVersion2) {
  // Send a spdy/3 frame on a spdy/2 codec
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY2);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  size_t unconsumed = parseSPDY(&codec, spdy3UnknownCtlFrame,
                                sizeof(spdy3UnknownCtlFrame), -1);
  EXPECT_EQ(unconsumed, 0);
  // Expect a GOAWAY with PROTOCOL_ERROR (because of unsupported version)
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.lastParseError->getCodecStatusCode(),
            spdy::rstToErrorCode(spdy::RST_PROTOCOL_ERROR));
}

TEST(SPDYCodecTest, UnsupportedFrameType) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  size_t unconsumed = parseSPDY(&codec, spdy3UnknownCtlFrame,
                                sizeof(spdy3UnknownCtlFrame), -1);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  // Spec says unknown control frames must be ignored
}

template <typename Codec>
unique_ptr<folly::IOBuf> getSynStream(Codec& egressCodec,
                                      uint32_t streamID,
                                      const HTTPMessage& msg,
                                      uint32_t assocStreamId = 0,
                                      bool eom = false,
                                      HTTPHeaderSize* size = nullptr) {
  folly::IOBufQueue output(folly::IOBufQueue::cacheChainLength());
  egressCodec.generateHeader(output, streamID, msg, assocStreamId, eom, size);
  return output.move();
}

template <typename Codec>
unique_ptr<folly::IOBuf> getSynStream(Codec& egressCodec,
                                      uint32_t streamID) {
  HTTPMessage msg;
  msg.setMethod("GET");
  msg.getHeaders().set("HOST", "www.foo.com");
  msg.setURL("https://www.foo.com");
  return getSynStream(egressCodec, streamID, msg);
}

// Runs the function with all combinations of spdy/2 and spdy/3
template <typename Codec, typename F, typename V>
void callFunction(F f, V version) {
  Codec ingressCodec(TransportDirection::DOWNSTREAM, version);
  Codec egressCodec(TransportDirection::UPSTREAM, version);
  f(ingressCodec, egressCodec);
}

#define copyForMe(F, C, V) callFunction<C>(F<C, C>,                 \
                                           SPDYVersion::V)

#define permuteTest(f) copyForMe(f, SPDYCodec, SPDY2);              \
                       copyForMe(f, SPDYCodec, SPDY3);

/**
 * Returns a SPDY frame with the specified version
 */
unique_ptr<folly::IOBuf> getVersionedSpdyFrame(const uint8_t* bytes,
                                               size_t len,
                                               uint8_t version) {
  auto frame = folly::IOBuf::copyBuffer(bytes, len);
  uint8_t* data = frame->writableData();
  data[1] = version; /* Set the version */
  return std::move(frame);
}

template <typename Codec1, typename Codec2>
void maxTransactionHelper(Codec1& ingressCodec, Codec2& egressCodec,
                          uint32_t parallel) {
  uint16_t expectedFailures = 0;
  uint16_t synCount = 101;
  ingressCodec.getEgressSettings()->setSetting(
    SettingsId::MAX_CONCURRENT_STREAMS, parallel);
  FakeHTTPCodecCallback callbacks;
  ingressCodec.setCallback(&callbacks);
  for (uint16_t i = 0; i < synCount; ++i) {
    if (i >= parallel) {
      ++expectedFailures;
    }
    auto toParse = getSynStream(egressCodec, 2*i + 1);
    ingressCodec.onIngress(*toParse);
    EXPECT_EQ(callbacks.sessionErrors, 0);
    EXPECT_EQ(callbacks.streamErrors, expectedFailures);
  }
}

template <typename Codec1, typename Codec2>
void doDefaultMaxTransactionTest(Codec1& ingressCodec, Codec2& egressCodec) {
  maxTransactionHelper(ingressCodec, egressCodec, 50);
}

template <typename Codec1, typename Codec2>
void doNonDefaultMaxTransactionTest(Codec1& ingressCodec, Codec2& egressCodec) {
  maxTransactionHelper(ingressCodec, egressCodec, 100);
}

TEST(SPDYCodecTest, DefaultMaxTransactions) {
  permuteTest(doDefaultMaxTransactionTest);
}

TEST(SPDYCodecTest, NonDefaultMaxTransactions) {
  permuteTest(doNonDefaultMaxTransactionTest);
}

template <typename Codec1, typename Codec2>
void doEmptyHeaderValueTest(Codec1& ingressCodec, Codec2& egressCodec) {
  uint8_t version = ingressCodec.getVersion();
  bool emptyAllowed = version != 2;
  FakeHTTPCodecCallback callbacks;
  ingressCodec.setCallback(&callbacks);
  HTTPMessage toSend;
  toSend.setMethod("GET");
  toSend.setURL("http://www.foo.com");
  auto& headers = toSend.getHeaders();
  headers.set("Host", "www.foo.com");
  headers.set("Pragma", "");
  headers.set("X-Test1", "yup");
  HTTPHeaderSize size;
  std::string pragmaValue;
  HTTPCodec::StreamID id(1);

  for (auto i = 0; i < 3; i++) {
    auto toParse = getSynStream(egressCodec, id + 2 * i,
                                toSend, 0, false, &size);
    ingressCodec.onIngress(*toParse);

    EXPECT_EQ(callbacks.sessionErrors, 0);
    EXPECT_EQ(callbacks.streamErrors, 0);
    ASSERT_NE(callbacks.msg.get(), nullptr);
    const auto& parsed = callbacks.msg->getHeaders();
    EXPECT_EQ(parsed.exists("Pragma"), emptyAllowed);
    EXPECT_EQ(parsed.exists("pragma"), emptyAllowed);
    EXPECT_EQ(parsed.getSingleOrEmpty("Pragma"), pragmaValue);
    EXPECT_EQ(parsed.getSingleOrEmpty("X-Test1"), "yup");
    // All codecs add the accept-encoding header
    EXPECT_EQ(parsed.exists("accept-encoding"), true);
    // SPDY/2 subtracts the Host header, but it should infer it from the
    // host:port portion of the requested url and present it in the headers
    EXPECT_EQ(parsed.exists("host"), true);
    EXPECT_EQ(callbacks.msg->getURL(), "http://www.foo.com");
    EXPECT_EQ(parsed.size(), emptyAllowed ? 4 : 3);
    EXPECT_TRUE(size.uncompressed > 0);
    EXPECT_TRUE(size.compressed > 0);

    if (i == 0) {
      headers.add("Pragma", "");
    }
    if (i == 1) {
      pragmaValue = "foo";
      headers.add("Pragma", pragmaValue);
      emptyAllowed = true; // SPDY/2 better have it now too
    }
  }
}

TEST(SPDYCodecTest, EmptyHeaderValue) {
  permuteTest(doEmptyHeaderValueTest);
}

/**
 * Tests a syn stream request containing the wrong version. This frame is
 * generated by using the v2 synStream firefox frame specified above and
 * changing the version field to 3. The dictionary used for the zlib compression
 * will be different and the session will be rejected.
 */
TEST(SPDYCodecTest, SynStreamWrongVersion) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3);
  FakeHTTPCodecCallback callbacks;
  codec.setCallback(&callbacks);
  auto frame = getVersionedSpdyFrame(synStream, sizeof(synStream), 3);
  size_t unconsumed = parseSPDY(&codec, frame->data(), frame->length(), -1);
  EXPECT_EQ(unconsumed, 0);
  // Since the session compression state is inconsistent we need to send a
  // session error. Expect a GOAWAY with PROTOCOL_ERROR.
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.lastParseError->getCodecStatusCode(),
            spdy::rstToErrorCode(spdy::RST_PROTOCOL_ERROR));
}

/**
 * A syn reply frame with invalid length
 */
uint8_t shortSynReply[] =
{ 0x80, 0x02, 0x00, 0x02, 0x01, 0x00, 0x00, 0x04, // length set to 4
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

template <typename Codec1, typename Codec2>
void doShortSynReplyTest(Codec1& ingressCodec, Codec2& egressCodec) {
  FakeHTTPCodecCallback callbacks;
  egressCodec.setCallback(&callbacks);
  auto frame = getVersionedSpdyFrame(shortSynReply, sizeof(shortSynReply),
                                     egressCodec.getVersion());
  egressCodec.onIngress(*frame);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 0);
}

TEST(SPDYCodecTest, ShortSynReply) {
  permuteTest(doShortSynReplyTest);
}

TEST(SPDYCodecTest, SupportsSessionFlowControl) {
  SPDYCodec spdy2(TransportDirection::UPSTREAM,
                  SPDYVersion::SPDY2);
  SPDYCodec spdy3(TransportDirection::UPSTREAM,
                  SPDYVersion::SPDY3);
  SPDYCodec spdy3_1(TransportDirection::UPSTREAM,
                    SPDYVersion::SPDY3_1);
  EXPECT_FALSE(spdy2.supportsSessionFlowControl());
  EXPECT_FALSE(spdy3.supportsSessionFlowControl());
  EXPECT_TRUE(spdy3_1.supportsSessionFlowControl());
}

// Test serializing and deserializing a header that has many values
TEST(SPDYCodecTest, HeaderWithManyValues) {
  const std::string kMultiValued = "X-Multi-Valued";
  const unsigned kNumValues = 1000;

  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::UPSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  HTTPMessage req;
  req.setMethod("GET");
  req.getHeaders().set("HOST", "www.foo.com");
  req.setURL("https://www.foo.com");
  for (unsigned i = 0; i < kNumValues; ++i) {
    req.getHeaders().add(kMultiValued, folly::to<string>("Value", i));
  }
  auto syn = getSynStream(egressCodec, 1, req);
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  CHECK_NOTNULL(callbacks.msg.get());
  EXPECT_EQ(callbacks.msg->getHeaders().getNumberOfValues(kMultiValued),
            kNumValues);
}

TEST(SPDYCodecTest, LargeFrameEncoding) {
  const std::string kMultiValued = "X-Multi-Valued";
  const unsigned kNumValues = 1000;

  SPDYCodec codec(TransportDirection::UPSTREAM, SPDYVersion::SPDY3);
  // This will simulate the condition where we have a very small
  // compresed headers size compared to the number of headers we
  // have.
  codec.setMaxUncompressedHeaders(0);
  auto req = getGetRequest();
  for (unsigned i = 0; i < kNumValues; ++i) {
    req.getHeaders().add(kMultiValued, folly::to<string>("Value", i));
  }
  auto syn = getSynStream(codec, 1, req);
}

// Test serializing and deserializing a header that has many values
TEST(SPDYCodecTest, InvalidSettings) {
  FakeHTTPCodecCallback callbacks;

  SPDYCodec egressCodec(TransportDirection::UPSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  folly::IOBufQueue output(folly::IOBufQueue::cacheChainLength());
  egressCodec.getEgressSettings()->setSetting(
    SettingsId::INITIAL_WINDOW_SIZE,
    (uint32_t)std::numeric_limits<int32_t>::max() + 1);
  egressCodec.generateSettings(output);
  auto ingress = output.move();
  ingressCodec.onIngress(*ingress);
  EXPECT_EQ(callbacks.messageBegin, 0);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
}

TEST(SPDYCodecTest, HeaderWithFin) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::UPSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  HTTPMessage req;
  req.setMethod("GET");
  req.getHeaders().set("HOST", "www.foo.com");
  req.setURL("https://www.foo.com/");
  auto syn = getSynStream(egressCodec, 1, req, 0, true /* eom */);
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.messageComplete, 1);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.assocStreamId, 0);
}

TEST(SPDYCodecTest, ServerPush) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::DOWNSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  HTTPMessage push;
  push.getHeaders().set("HOST", "www.foo.com");
  push.setURL("https://www.foo.com/");
  auto syn = getSynStream(egressCodec, 2, push, 1);
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.assocStreamId, 1);
}

/**
 * A push stream with Host header missing
 */
uint8_t pushStreamWithHostMissing[] =
{  0x80, 0x3, 0x0, 0x1, 0x2, 0x0, 0x0, 0x7c,
   0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x1,
   0x0, 0x0, 0x38, 0x30, 0xe3, 0xc6, 0xa7, 0xc2,
   0x0, 0x62, 0x0, 0x9d, 0xff, 0x0, 0x0, 0x0,
   0x4, 0x0, 0x0, 0x0, 0x5, 0x3a, 0x70, 0x61,
   0x74, 0x68, 0x0, 0x0, 0x0, 0x14, 0x68, 0x74,
   0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77,
   0x77, 0x2e, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f,
   0x6d, 0x2f, 0x0, 0x0, 0x0, 0x7, 0x3a, 0x73,
   0x63, 0x68, 0x65, 0x6d, 0x65, 0x0, 0x0, 0x0,
   0x4, 0x68, 0x74, 0x74, 0x70, 0x0, 0x0, 0x0,
   0x7, 0x3a, 0x73, 0x74, 0x61, 0x74, 0x75, 0x73,
   0x0, 0x0, 0x0, 0x3, 0x32, 0x30, 0x30, 0x0,
   0x0, 0x0, 0x8, 0x3a, 0x76, 0x65, 0x72, 0x73,
   0x69, 0x6f, 0x6e, 0x0, 0x0, 0x0, 0x8, 0x48,
   0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x31, 0x0,
   0x0, 0x0, 0xff, 0xff
};

TEST(SPDYCodecTest, ServerPushHostMissing) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  auto syn = folly::IOBuf::copyBuffer(pushStreamWithHostMissing,
                                      sizeof(pushStreamWithHostMissing));
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 0);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.assocStreamId, 0);
}

/**
 * A push stream with an odd StreamID
 */
uint8_t pushStreamWithOddId[] =
{ 0x80, 0x03, 0x00, 0x01, 0x02, 0x00, 0x00, 0x91,
  0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01,
  0x00, 0x00, 0x38, 0x30, 0xe3, 0xc6, 0xa7, 0xc2,
  0x00, 0x77, 0x00, 0x88, 0xff, 0x00, 0x00, 0x00,
  0x05, 0x00, 0x00, 0x00, 0x05, 0x3a, 0x68, 0x6f,
  0x73, 0x74, 0x00, 0x00, 0x00, 0x0b, 0x77, 0x77,
  0x77, 0x2e, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f,
  0x6d, 0x00, 0x00, 0x00, 0x07, 0x3a, 0x6d, 0x65,
  0x74, 0x68, 0x6f, 0x64, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x05, 0x3a, 0x70, 0x61, 0x74,
  0x68, 0x00, 0x00, 0x00, 0x14, 0x68, 0x74, 0x74,
  0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77,
  0x2e, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x6d,
  0x2f, 0x00, 0x00, 0x00, 0x07, 0x3a, 0x73, 0x63,
  0x68, 0x65, 0x6d, 0x65, 0x00, 0x00, 0x00, 0x04,
  0x68, 0x74, 0x74, 0x70, 0x00, 0x00, 0x00, 0x08,
  0x3a, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e,
  0x00, 0x00, 0x00, 0x08, 0x48, 0x54, 0x54, 0x50,
  0x2f, 0x31, 0x2e, 0x31, 0x00, 0x00, 0x00, 0xff,
  0xff
};

TEST(SPDYCodecTest, ServerPushInvalidId) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  auto syn = folly::IOBuf::copyBuffer(pushStreamWithOddId,
                                      sizeof(pushStreamWithOddId));
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 0);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.assocStreamId, 0);
}

/**
 * A push stream without unidirectional flag
 */
uint8_t pushStreamWithoutUnidirectional[] =
{ 0x80, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x91,
  0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
  0x00, 0x00, 0x38, 0x30, 0xe3, 0xc6, 0xa7, 0xc2,
  0x00, 0x77, 0x00, 0x88, 0xff, 0x00, 0x00, 0x00,
  0x05, 0x00, 0x00, 0x00, 0x05, 0x3a, 0x68, 0x6f,
  0x73, 0x74, 0x00, 0x00, 0x00, 0x0b, 0x77, 0x77,
  0x77, 0x2e, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f,
  0x6d, 0x00, 0x00, 0x00, 0x07, 0x3a, 0x6d, 0x65,
  0x74, 0x68, 0x6f, 0x64, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x05, 0x3a, 0x70, 0x61, 0x74,
  0x68, 0x00, 0x00, 0x00, 0x14, 0x68, 0x74, 0x74,
  0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77,
  0x2e, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x6d,
  0x2f, 0x00, 0x00, 0x00, 0x07, 0x3a, 0x73, 0x63,
  0x68, 0x65, 0x6d, 0x65, 0x00, 0x00, 0x00, 0x04,
  0x68, 0x74, 0x74, 0x70, 0x00, 0x00, 0x00, 0x08,
  0x3a, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e,
  0x00, 0x00, 0x00, 0x08, 0x48, 0x54, 0x54, 0x50,
  0x2f, 0x31, 0x2e, 0x31, 0x00, 0x00, 0x00, 0xff,
  0xff
};

TEST(SPDYCodecTest, ServerPushInvalidFlags) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  auto syn = folly::IOBuf::copyBuffer(pushStreamWithoutUnidirectional,
                                      sizeof(pushStreamWithoutUnidirectional));
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 0);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.assocStreamId, 0);
}

/**
 * A push stream with assocStreamID = 0
 */
uint8_t pushStreamWithoutAssoc[] =
{ 0x80, 0x03, 0x00, 0x01, 0x02, 0x00, 0x00, 0x91,
  0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x38, 0x30, 0xe3, 0xc6, 0xa7, 0xc2,
  0x00, 0x77, 0x00, 0x88, 0xff, 0x00, 0x00, 0x00,
  0x05, 0x00, 0x00, 0x00, 0x05, 0x3a, 0x68, 0x6f,
  0x73, 0x74, 0x00, 0x00, 0x00, 0x0b, 0x77, 0x77,
  0x77, 0x2e, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f,
  0x6d, 0x00, 0x00, 0x00, 0x07, 0x3a, 0x6d, 0x65,
  0x74, 0x68, 0x6f, 0x64, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x05, 0x3a, 0x70, 0x61, 0x74,
  0x68, 0x00, 0x00, 0x00, 0x14, 0x68, 0x74, 0x74,
  0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77,
  0x2e, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x6d,
  0x2f, 0x00, 0x00, 0x00, 0x07, 0x3a, 0x73, 0x63,
  0x68, 0x65, 0x6d, 0x65, 0x00, 0x00, 0x00, 0x04,
  0x68, 0x74, 0x74, 0x70, 0x00, 0x00, 0x00, 0x08,
  0x3a, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e,
  0x00, 0x00, 0x00, 0x08, 0x48, 0x54, 0x54, 0x50,
  0x2f, 0x31, 0x2e, 0x31, 0x00, 0x00, 0x00, 0xff,
  0xff
};

TEST(SPDYCodecTest, ServerPushWithoutAssoc) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  auto syn = folly::IOBuf::copyBuffer(pushStreamWithoutAssoc,
                                      sizeof(pushStreamWithoutAssoc));
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 0);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.assocStreamId, 0);
}

TEST(SPDYCodecTest, StatusReason) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::DOWNSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  HTTPMessage resp;
  resp.setStatusCode(200);
  auto syn = getSynStream(egressCodec, 1, resp, 0);
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.msg->getStatusCode(), 200);
  EXPECT_EQ(callbacks.msg->getStatusMessage(), "");
  callbacks.reset();

  resp.setStatusCode(200);
  resp.setStatusMessage("Awesome");
  syn = getSynStream(egressCodec, 1, resp, 0);
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.msg->getStatusCode(), 200);
  EXPECT_EQ(callbacks.msg->getStatusMessage(), "Awesome");
  callbacks.reset();

  // Out of range
  resp.setStatusCode(2000);
  resp.setStatusMessage("10x OK");
  syn = getSynStream(egressCodec, 1, resp, 0);
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 0);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_TRUE(callbacks.lastParseError->hasCodecStatusCode());
  EXPECT_EQ(callbacks.lastParseError->getCodecStatusCode(),
            spdy::rstToErrorCode(spdy::RST_PROTOCOL_ERROR));
  callbacks.reset();

  resp.setStatusCode(64);
  resp.setStatusMessage("Ought to be enough for anybody");
  syn = getSynStream(egressCodec, 1, resp, 0);
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 0);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_TRUE(callbacks.lastParseError->hasCodecStatusCode());
  EXPECT_EQ(callbacks.lastParseError->getCodecStatusCode(),
            spdy::rstToErrorCode(spdy::RST_PROTOCOL_ERROR));
  callbacks.reset();
}

TEST(SPDYCodecTest, UpstreamPing) {
  folly::IOBufQueue buf(folly::IOBufQueue::cacheChainLength());
  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::UPSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  // Send a reply with no corresponding ping request
  egressCodec.generatePingReply(buf, 2);
  auto pingReply = buf.move();
  ingressCodec.onIngress(*pingReply);
  ASSERT_EQ(callbacks.recvPingReply, 0); // should be ignored

  auto lastRequest = callbacks.recvPingRequest;
  for (unsigned i = 0; i < 10; ++i) {
    egressCodec.generatePingRequest(buf);
    auto pingReq = buf.move();
    ingressCodec.onIngress(*pingReq);
    ASSERT_GT(callbacks.recvPingRequest, lastRequest);
    ASSERT_EQ(callbacks.recvPingRequest % 2, 1);
    lastRequest = callbacks.recvPingRequest;
  }
}

TEST(SPDYCodecTest, DownstreamPing) {
  folly::IOBufQueue buf(folly::IOBufQueue::cacheChainLength());
  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::DOWNSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  // Send a reply with no corresponding ping request
  egressCodec.generatePingReply(buf, 1);
  auto pingReply = buf.move();
  ingressCodec.onIngress(*pingReply);
  ASSERT_EQ(callbacks.recvPingReply, 0); // should be ignored

  auto lastRequest = callbacks.recvPingRequest;
  for (unsigned i = 0; i < 10; ++i) {
    egressCodec.generatePingRequest(buf);
    auto pingReq = buf.move();
    ingressCodec.onIngress(*pingReq);
    ASSERT_GT(callbacks.recvPingRequest, lastRequest);
    ASSERT_EQ(callbacks.recvPingRequest % 2, 0);
    lastRequest = callbacks.recvPingRequest;
  }
}

TEST(SPDYCodecTest, DateHeader) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::DOWNSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  HTTPMessage resp;
  resp.setStatusCode(200);
  auto syn = getSynStream(egressCodec, 1, resp, 0);
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_TRUE(callbacks.msg->getHeaders().exists(HTTP_HEADER_DATE));
}

// SYN_STREAM includes ~100k header name with 50k one-byte values
uint8_t multiValuedHeaderAttack[] =
{ 0x80, 0x03, 0x00, 0x01, 0x01, 0x00, 0x02, 0x11,
  0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
  0x60, 0x00, 0x38, 0xea, 0xe3, 0xc6, 0xa7, 0xc2,
  0xec, 0xd4, 0x31, 0x0e, 0x82, 0x40, 0x10, 0x46,
  0x61, 0x8c, 0x85, 0x1a, 0x43, 0x6f, 0x69, 0x4d,
  0x31, 0x24, 0xda, 0x6d, 0x4f, 0xb8, 0x80, 0x17,
  0x00, 0x8d, 0xa1, 0xd0, 0x30, 0xc9, 0x6e, 0x01,
  0x57, 0xb7, 0x72, 0x18, 0x49, 0x28, 0xe4, 0x08,
  0xef, 0xab, 0x61, 0x93, 0x4d, 0xfe, 0x7d, 0xb6,
  0xa3, 0xc3, 0xf4, 0x9c, 0xc2, 0x1c, 0x84, 0x93,
  0x2d, 0x5a, 0xfa, 0x94, 0x7a, 0x89, 0xad, 0x3c,
  0x2d, 0xbd, 0xad, 0x8d, 0x4f, 0xee, 0x1e, 0x8d,
  0x5d, 0x58, 0xa6, 0x5d, 0x57, 0x37, 0xff, 0x4d,
  0x1b, 0x0f, 0xd8, 0xb1, 0xfc, 0x8d, 0x5c, 0xb4,
  0x53, 0xff, 0x32, 0x5a, 0x38, 0xdf, 0x5e, 0xd7,
  0x2e, 0x25, 0x9d, 0xc6, 0xbf, 0x0f, 0xeb, 0x9b,
  0x5f, 0xc2, 0xbe, 0x2d, 0xca, 0x62, 0xbd, 0xe6,
  0xb9, 0x5f, 0xf2, 0x3c, 0xdf, 0xf2, 0xef, 0x81,
  0xe6, 0x51, 0x1f, 0xe3, 0xab, 0x19, 0xed, 0xc4,
  0x8b, 0x5c, 0xb3, 0xcd, 0x27, 0x1b, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8,
  0xb2, 0x07, 0x07, 0x02, 0x00, 0x00, 0x00, 0x00,
  0x40, 0xfe, 0xaf, 0x8d, 0xa0, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xc2, 0x1e, 0x1c, 0x08,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xf9, 0xbf, 0x36,
  0x82, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0x0a, 0x7b, 0x70, 0x20, 0x00, 0x00, 0x00, 0x00,
  0x00, 0xe4, 0xff, 0xda, 0x08, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0x2a, 0xec, 0xc1, 0x41,
  0x01, 0x00, 0x00, 0x04, 0x04, 0xb0, 0x53, 0x47,
  0x3f, 0xe5, 0xbd, 0x14, 0x10, 0x61, 0x1b, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0xf0, 0x48, 0x6d, 0x7a, 0xa2, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0x7a, 0xec, 0xc2, 0x01, 0x09, 0x00, 0x00, 0x00,
  0x00, 0x90, 0xff, 0xaf, 0x1d, 0x11, 0x55, 0x55,
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
  0x55, 0x55, 0x55, 0x55, 0x55, 0x15, 0x76, 0xe1,
  0x80, 0x04, 0x00, 0x00, 0x00, 0x00, 0xc8, 0xff,
  0xd7, 0x8e, 0x88, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
  0xaa, 0xaa, 0x0a, 0xbb, 0x70, 0x40, 0x02, 0x00,
  0x00, 0x00, 0x00, 0xe4, 0xff, 0x6b, 0x47, 0x44,
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x85,
  0x5d, 0x38, 0xa6, 0x01, 0x00, 0x00, 0x60, 0x18,
  0xe4, 0x67, 0xfe, 0x05, 0xf6, 0x9f, 0x06, 0x02,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0xbc, 0x05, 0x00, 0x00, 0xff,
  0xff
};

// Ensure the codec can handle a malicious packet intended to exhaust server
// resources
TEST(SPDYCodecTest, HeaderDoS) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  auto attack = folly::IOBuf::copyBuffer(multiValuedHeaderAttack,
                                         sizeof(multiValuedHeaderAttack));

  ingressCodec.onIngress(*attack);
  EXPECT_EQ(callbacks.messageBegin, 0);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.sessionErrors, 1);
}

// Make sure SPDYCodec behaves correctly when we generate and receive double
// GOAWAYs.
TEST(SPDYCodecTest, DoubleGoawayServer) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::DOWNSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);

  egressCodec.enableDoubleGoawayDrain();
  ingressCodec.setCallback(&callbacks);
  egressCodec.setCallback(&callbacks);

  unsigned ack = std::numeric_limits<int32_t>::max();
  auto f = [&] () {
    folly::IOBufQueue output(folly::IOBufQueue::cacheChainLength());
    egressCodec.generateGoaway(output, ack, ErrorCode::NO_ERROR);
    auto ingress = output.move();
    ingressCodec.onIngress(*ingress);
    ack -= 2;
  };

  EXPECT_TRUE(egressCodec.isReusable());
  EXPECT_FALSE(egressCodec.isWaitingToDrain());
  EXPECT_TRUE(ingressCodec.isReusable());
  f();
  // server spdy codec remains reusable after the first goaway
  EXPECT_TRUE(egressCodec.isReusable());
  EXPECT_TRUE(egressCodec.isWaitingToDrain());
  f();
  EXPECT_FALSE(egressCodec.isReusable());
  EXPECT_FALSE(egressCodec.isWaitingToDrain());

  EXPECT_EQ(2, callbacks.goaways);
}

TEST(SPDYCodecTest, DoubleGoawayClient) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::UPSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3);

  egressCodec.enableDoubleGoawayDrain();
  ingressCodec.setCallback(&callbacks);
  egressCodec.setCallback(&callbacks);

  unsigned ack = std::numeric_limits<int32_t>::max();
  auto f = [&] () {
    folly::IOBufQueue output(folly::IOBufQueue::cacheChainLength());
    egressCodec.generateGoaway(output, ack, ErrorCode::NO_ERROR);
    auto ingress = output.move();
    ingressCodec.onIngress(*ingress);
    ack -= 2;
  };

  EXPECT_TRUE(egressCodec.isReusable());
  EXPECT_FALSE(egressCodec.isWaitingToDrain());
  f();
  // client spdy codec not reusable after the first goaway
  EXPECT_FALSE(egressCodec.isReusable());
  EXPECT_TRUE(egressCodec.isWaitingToDrain());
  EXPECT_FALSE(ingressCodec.isReusable());
  f();
  EXPECT_FALSE(egressCodec.isReusable());
  EXPECT_FALSE(egressCodec.isWaitingToDrain());
  EXPECT_FALSE(ingressCodec.isReusable());

  EXPECT_EQ(2, callbacks.goaways);
}


TEST(SPDYCodecTest, SingleGoawayClient) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::UPSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3);

  ingressCodec.setCallback(&callbacks);
  egressCodec.setCallback(&callbacks);

  unsigned ack = 0;
  auto f = [&] () {
    folly::IOBufQueue output(folly::IOBufQueue::cacheChainLength());
    egressCodec.generateGoaway(output, ack, ErrorCode::NO_ERROR);
    auto ingress = output.move();
    ingressCodec.onIngress(*ingress);
    ack -= 2;
  };

  EXPECT_TRUE(egressCodec.isReusable());
  EXPECT_TRUE(egressCodec.isWaitingToDrain());
  f();
  // client spdy codec not reusable after the first goaway
  EXPECT_FALSE(egressCodec.isReusable());
  EXPECT_FALSE(egressCodec.isWaitingToDrain());
  EXPECT_FALSE(ingressCodec.isReusable());

  EXPECT_EQ(1, callbacks.goaways);
}

// Two packets:
//  - first one has an invalid header nanme
//  - second one has an empty header block
uint8_t invalidHeaderPlusEmptyBlock[] =
{ 0x80, 0x03, 0x00, 0x02, 0xc4, 0x00, 0x00, 0x14,
  0xf7, 0x76, 0x2d, 0x37, 0x78, 0x9c, 0x93, 0x60,
  0x00, 0x03, 0x75, 0x06, 0xbd, 0x76, 0x21, 0xb2,
  0xd0, 0xd9, 0x54, 0x91, 0x80, 0x03, 0x00, 0x01,
  0x4e, 0x00, 0x00, 0x0a, 0xbe, 0x14, 0x31, 0x55,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00
};

// Try to trick the codec into leaving the HeaderList in an inconsistent state
// (a header name without a corresponding value), and parsing this inconsistent
// HeaderList
TEST(SPDYCodecTest, OddHeaderListTest) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  auto attack = folly::IOBuf::copyBuffer(invalidHeaderPlusEmptyBlock,
                                         sizeof(invalidHeaderPlusEmptyBlock));

  ingressCodec.onIngress(*attack);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.streamErrors, 1);
}

// Send a RST_STREAM for stream=1 while parsing a SYN_STREAM+FIN for stream=3.
// Stream 3 should be unaffected
TEST(SPDYCodecTest, SendRstParsingFrame) {
  NiceMock<MockHTTPCodecCallback> callbacks;
  SPDYCodec egressCodec(TransportDirection::UPSTREAM,
                        SPDYVersion::SPDY3_1);
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3_1);
  folly::IOBufQueue egressCodecQueue(folly::IOBufQueue::cacheChainLength());
  folly::IOBufQueue ingressCodecQueue(folly::IOBufQueue::cacheChainLength());

  InSequence enforceOrder;

  EXPECT_CALL(callbacks, onHeadersComplete(3, _))
    .WillOnce(InvokeWithoutArgs([&] {
          ingressCodec.generateRstStream(ingressCodecQueue,
                                         1, ErrorCode::CANCEL);
        }));
  EXPECT_CALL(callbacks, onMessageComplete(3, false));

  ingressCodec.setCallback(&callbacks);
  auto syn = getSynStream(egressCodec, 3);
  egressCodecQueue.append(std::move(syn));
  egressCodec.generateEOM(egressCodecQueue, 3);
  auto ingress = egressCodecQueue.move();
  ingress->coalesce();
  ingressCodec.onIngress(*ingress);
}

uint8_t invalidNumNameValuesBlock[] = {
  0x80, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x91,
  0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
  0x00, 0x00, 0x38, 0x30, 0xe3, 0xc6, 0xa7, 0xc2,
  0x00, 0x77, 0x00, 0x88, 0xff, 0x00, 0x00, 0x00,
  0x05, 0x00, 0x00, 0x00, 0x05, 0x3a, 0x68, 0x6f,
  0x73, 0x74, 0x00, 0x00, 0x00, 0x0b, 0x77, 0x77,
  0x77, 0x2e, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f,
  0x6d, 0x00, 0x00, 0x00, 0x77, 0x3a, 0x6d, 0x65,
  0x74, 0x68, 0x6f, 0x64, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x05, 0x3a, 0x70, 0x61, 0x74,
  0x68, 0x00, 0x13, 0x00, 0x14, 0x68, 0x74, 0x74,
  0x39, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77,
  0x2e, 0x66, 0x6f, 0x6f, 0x2e, 0x63, 0x6f, 0x6d,
  0x2f, 0x00, 0x00, 0x00, 0x07, 0x3a, 0x73, 0x63,
  0x68, 0x65, 0x6d, 0x65, 0x00, 0x00, 0x00, 0x04,
  0x68, 0x74, 0x74, 0x70, 0x00, 0x00, 0x00, 0x08,
  0x3a, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e,
  0x00, 0x00, 0x00, 0x08, 0x48, 0x54, 0x54, 0x50,
  0x2f, 0x31, 0x2e, 0x31, 0x00, 0x00, 0x00, 0xff,
  0xff
};
// Ensure we generate a session-level error if the number of name values
// exceeds the size of our current buffer.
TEST(SPDYCodecTest, BadNumNameValues) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3_1);
  ingressCodec.setCallback(&callbacks);

  auto attack = folly::IOBuf::copyBuffer(invalidNumNameValuesBlock,
                                         sizeof(invalidNumNameValuesBlock));

  ingressCodec.onIngress(*attack);
  EXPECT_EQ(callbacks.messageBegin, 0);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
}

const uint8_t kShortSettings[] = {
  0x80, 0x03, 0x00, 0x04, 0xee, 0x00, 0x00, 0x01, 0x00, 0x00
};
// Make sure we reject SETTINGS frames that are too short with GOAWAY
TEST(SPDYCodecTest, ShortSettings) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3_1);
  ingressCodec.setCallback(&callbacks);

  auto attack = folly::IOBuf::copyBuffer(kShortSettings,
                                         sizeof(kShortSettings));

  ingressCodec.onIngress(*attack);
  EXPECT_EQ(callbacks.messageBegin, 0);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
}

TEST(SPDYCodecTest, SegmentedHeaderBlock) {
  SPDYCodec egressCodec(TransportDirection::UPSTREAM,
                        SPDYVersion::SPDY3_1_HPACK);
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3_1_HPACK);
  // generate huge string to use as a header value
  string huge;
  uint32_t size = 20000;
  char ch = 'a';
  for (uint32_t i = 0; i < size; i++) {
    huge.push_back(ch);
    if (ch == 'z') {
      ch = 'a';
    } else {
      ch++;
    }
  }
  HTTPMessage req;
  req.setMethod("GET");
  req.setURL("http://www.facebook.com");
  auto& reqHeaders = req.getHeaders();
  reqHeaders.set("HOST", "www.facebook.com");
  // setting this huge header value will cause allocation of a separate IOBuf
  reqHeaders.set("X-FB-Huge", huge);
  auto buf = getSynStream(egressCodec, 1, req);
  FakeHTTPCodecCallback callbacks;
  ingressCodec.setCallback(&callbacks);
  ingressCodec.onIngress(*buf);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.msg->getHeaders().getSingleOrEmpty("x-fb-huge").size(),
            size);

  // do it for responses
  HTTPMessage resp;
  resp.setStatusCode(200);
  resp.setStatusMessage("OK");
  auto& respHeaders = resp.getHeaders();
  respHeaders.set("X-FB-Huge", huge);
  auto buf2 = getSynStream(ingressCodec, 1, resp);
  callbacks.reset();
  egressCodec.setCallback(&callbacks);
  egressCodec.onIngress(*buf2);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.msg->getHeaders().getSingleOrEmpty("x-fb-huge").size(),
            size);
}

const uint8_t kColonHeaders[] = {
  0x80, 0x03, 0x00, 0x01, 0x48, 0x00, 0x00, 0x1a, 0xf6, 0xf6, 0x1a, 0xb5,
  0x00, 0x00, 0x00, 0x00, 0x17, 0x28, 0x28, 0x53, 0x62, 0x60, 0x60, 0x10,
  0x60, 0x60, 0x60, 0x60, 0xb4, 0x1a, 0xbc, 0x84, 0xa4, 0xa4
};

// Test a case where we use a single colon as a header name
TEST(SPDYCodecTest, ColonHeaders) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3_1);
  ingressCodec.setCallback(&callbacks);

  auto testBuf = folly::IOBuf::copyBuffer(kColonHeaders,
                                         sizeof(kColonHeaders));

  ingressCodec.onIngress(*testBuf);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.sessionErrors, 0);
}

// this magic blob contains a pair of invalid header "key='', value='x'" encoded
// with HPACK. It's not easy to generate it, as any use of HTTPHeaders will
// hit an assert for name size before we have the chance to decode
const uint8_t kBufEmptyHeader[] = {
  0x80, 0x3, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0e, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x80, 0x81, 0xea
};

/**
 * make sure we're not hitting CHECK on header name size when receiving a rogue
 * header from the client
 */
TEST(SPDYCodecTest, EmptyHeaderName) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3_1_HPACK);

  auto testBuf = IOBuf::copyBuffer(kBufEmptyHeader, sizeof(kBufEmptyHeader));
  ingressCodec.setCallback(&callbacks);
  ingressCodec.onIngress(*testBuf);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.sessionErrors, 0);
}
