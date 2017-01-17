/*
 *  Copyright (c) 2017, Facebook, Inc.
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
#include <proxygen/lib/http/codec/SPDYCodec.h>
#include <proxygen/lib/http/codec/SPDYConstants.h>
#include <proxygen/lib/http/codec/SPDYVersionSettings.h>
#include <proxygen/lib/http/codec/test/HTTPParallelCodecTest.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <random>

using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;

size_t parseSPDY(SPDYCodec* codec, const uint8_t* inputData, uint32_t length,
                 int32_t atOnce, FakeHTTPCodecCallback& callbacks) {
  codec->setCallback(&callbacks);
  return parse(codec, inputData, length, atOnce, callbacks.getStopFn());
}

uint8_t shortSynStream[] =
{ 0x80, 0x03, 0x00, 0x01,
  0x01, 0x00, 0x00, 0x04,  // length must be >= 12
  0x61, 0x62, 0x63, 0x64
};

TEST(SPDYCodecTest, JunkSPDY) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3);
  FakeHTTPCodecCallback callbacks;
  size_t unconsumed =
    parseSPDY(&codec, shortSynStream, sizeof(shortSynStream), -1, callbacks);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 0);
}

uint8_t longPing[] =
{ 0x80, 0x03, 0x00, 0x06,
  0x00, 0x00, 0x00, 0x05,
  0x00, 0x00, 0x00, 0x00,
  0x00
};

TEST(SPDYCodecTest, LongPing) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3);
  FakeHTTPCodecCallback callbacks;
  size_t unconsumed =
    parseSPDY(&codec, longPing, sizeof(longPing), -1, callbacks);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 0);
}

uint8_t badType[] =
{ 0x80, 0x03, 0x00, 0x0A,
  0x00, 0x00, 0x00, 0x05,
  0x00, 0x00, 0x00, 0x00,
  0x00
};

TEST(SPDYCodecTest, BadType) {
  // If an endpoint receives a control frame for a type it does not recognize,
  // it must ignore the frame.
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3);
  FakeHTTPCodecCallback callbacks;
  size_t unconsumed =
    parseSPDY(&codec, badType, sizeof(badType), -1, callbacks);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
}

/**
 * A request from firefox for facebook.com
 */
uint8_t synStream[] =
{
0x80, 0x03, 0x00, 0x01, 0x01, 0x00, 0x04, 0x1a,
0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x38, 0xea, 0xe3, 0xc6, 0xa7, 0xc2,
0x02, 0x65, 0x57, 0x50, 0x22, 0xb4, 0xc2, 0x9e,
0x20, 0xd9, 0xad, 0x10, 0xa9, 0xd9, 0xdd, 0x35,
0x04, 0x94, 0xf1, 0xac, 0xa0, 0x45, 0x87, 0x14,
0x30, 0xed, 0xeb, 0x25, 0xa6, 0x15, 0x65, 0xe6,
0xa5, 0xe8, 0x15, 0x27, 0xe9, 0xa5, 0x01, 0x8b,
0xe9, 0x24, 0x60, 0x42, 0xd5, 0x4b, 0x06, 0x17,
0x30, 0xec, 0x56, 0xc5, 0xc0, 0xa2, 0x33, 0x17,
0x5c, 0xbe, 0x66, 0x94, 0x94, 0x14, 0x14, 0x83,
0xb5, 0x16, 0x24, 0x82, 0x8b, 0x3b, 0x6e, 0x80,
0x00, 0xd2, 0x2f, 0x2e, 0x49, 0x2c, 0x29, 0x2d,
0xd6, 0x2b, 0xc8, 0x28, 0x00, 0x08, 0x20, 0x94,
0x22, 0x80, 0x03, 0x20, 0x80, 0xf2, 0xf2, 0x75,
0x93, 0x13, 0x93, 0x33, 0x52, 0x01, 0x02, 0x08,
0x5b, 0xe1, 0xcb, 0x01, 0x10, 0x40, 0x79, 0xf9,
0xba, 0xc9, 0x89, 0xc9, 0x19, 0xa9, 0x00, 0x01,
0x84, 0x52, 0x1b, 0x78, 0x21, 0x42, 0x1f, 0x7b,
0x78, 0xa2, 0x85, 0xba, 0x35, 0xc8, 0xd7, 0x96,
0xd0, 0x58, 0x29, 0x4f, 0x4d, 0x2a, 0xd0, 0xd1,
0xd2, 0xd7, 0x02, 0x0b, 0x5a, 0x00, 0x4d, 0x93,
0x84, 0x96, 0x01, 0xba, 0x99, 0x79, 0xc5, 0xa9,
0xc9, 0xa5, 0x45, 0xa9, 0xba, 0x45, 0x90, 0x84,
0x0b, 0x72, 0x3f, 0x23, 0x40, 0x00, 0x19, 0x02,
0x04, 0x10, 0x46, 0xd1, 0x50, 0xee, 0x9b, 0x5f,
0x95, 0x99, 0x93, 0x93, 0xa8, 0x6f, 0xaa, 0x67,
0xa0, 0xa0, 0xe1, 0x9b, 0x98, 0x9c, 0x99, 0x57,
0x92, 0x5f, 0x9c, 0x61, 0x0d, 0x4e, 0xba, 0x39,
0xc0, 0x14, 0x97, 0xac, 0xe0, 0x1f, 0xac, 0x10,
0xa1, 0x60, 0x68, 0x10, 0x0f, 0x44, 0xa6, 0x9a,
0xc0, 0x74, 0x08, 0x4c, 0xb5, 0xe1, 0xa9, 0x49,
0xde, 0x99, 0x25, 0xfa, 0xa6, 0xc6, 0xe6, 0x7a,
0xc6, 0x66, 0x0a, 0x1a, 0xde, 0x1e, 0x21, 0xbe,
0x3e, 0x3a, 0x0a, 0x39, 0x99, 0xd9, 0xa9, 0x0a,
0xee, 0xa9, 0xc9, 0xd9, 0xf9, 0x9a, 0x0a, 0xce,
0x19, 0xc0, 0x72, 0x3a, 0x55, 0xdf, 0xc4, 0x42,
0xcf, 0x40, 0xcf, 0xc8, 0xd4, 0x18, 0x64, 0x7a,
0x70, 0x62, 0x5a, 0x62, 0x51, 0x26, 0x54, 0x13,
0xf6, 0x4a, 0x4f, 0x18, 0x9c, 0x16, 0x14, 0x60,
0x89, 0x41, 0x01, 0x94, 0x1a, 0x90, 0x54, 0x22,
0x57, 0x41, 0xa9, 0x79, 0xba, 0xa1, 0xc1, 0xc0,
0x34, 0x00, 0xf7, 0x39, 0x1b, 0xac, 0x94, 0x61,
0x7a, 0x0c, 0x10, 0x40, 0xb9, 0xf1, 0x25, 0xc5,
0xb6, 0x86, 0x26, 0x86, 0x66, 0x96, 0xa6, 0x86,
0xe6, 0x06, 0x66, 0xd6, 0x00, 0x01, 0xa4, 0x90,
0x92, 0x58, 0x52, 0x64, 0xeb, 0x1b, 0x5f, 0x60,
0x10, 0xe6, 0x58, 0x92, 0x15, 0x6e, 0xe9, 0x92,
0x57, 0x61, 0xe9, 0x1b, 0x16, 0xe8, 0xe9, 0x13,
0xef, 0x98, 0x9d, 0x66, 0x69, 0x0d, 0x10, 0x40,
0x0a, 0xf1, 0xe9, 0x89, 0xb6, 0xee, 0x8e, 0x86,
0x7a, 0x46, 0x7a, 0x86, 0x46, 0x46, 0x86, 0xe6,
0x66, 0x66, 0x16, 0x66, 0x96, 0x7a, 0x86, 0x26,
0xc6, 0xc6, 0xe6, 0x96, 0xc6, 0x26, 0x26, 0x26,
0xd6, 0x00, 0x01, 0xa4, 0x90, 0x53, 0x6a, 0x1b,
0x92, 0xee, 0x53, 0x18, 0x5a, 0xe2, 0x63, 0xee,
0xe5, 0x9a, 0xa9, 0xeb, 0xef, 0x5e, 0x98, 0x96,
0x14, 0x98, 0xeb, 0xe1, 0x5d, 0x9a, 0x51, 0x6e,
0x0d, 0x10, 0x40, 0x0a, 0x15, 0x25, 0x45, 0x79,
0xb6, 0x8e, 0x21, 0xa6, 0xc1, 0xc6, 0xc5, 0xba,
0x61, 0xa6, 0x7e, 0x49, 0xa5, 0x65, 0x51, 0xa1,
0xce, 0x29, 0xc9, 0x8e, 0xd6, 0x00, 0x01, 0x08,
0x82, 0x43, 0x23, 0x86, 0x61, 0x20, 0x00, 0x82,
0xad, 0x84, 0x1c, 0xf4, 0x8c, 0xfe, 0x5e, 0x8a,
0x51, 0x80, 0xa2, 0x4a, 0x6c, 0x68, 0x31, 0xcb,
0x24, 0xdd, 0x67, 0xf7, 0x75, 0x44, 0xfc, 0x3e,
0xec, 0x5f, 0x74, 0xdd, 0x28, 0xd9, 0x0b, 0x0e,
0x74, 0xdd, 0xdb, 0xb3, 0x50, 0xb2, 0x47, 0x8d,
0xac, 0x91, 0xb5, 0xb4, 0x30, 0x71, 0xa0, 0xd7,
0x44, 0xc9, 0x5e, 0x70, 0xa0, 0xd7, 0xdc, 0x9e,
0x85, 0x92, 0x3d, 0x6a, 0xd5, 0xb6, 0xe7, 0xbb,
0x94, 0xda, 0x70, 0xa0, 0xf3, 0x44, 0xc9, 0x1e,
0x38, 0xd0, 0x79, 0x6e, 0xcf, 0x42, 0xc9, 0x3f,
0x41, 0x70, 0x8e, 0x03, 0x20, 0x08, 0x00, 0x01,
0xf0, 0x35, 0x5b, 0x9a, 0xb0, 0x07, 0x51, 0x62,
0x45, 0xf0, 0x27, 0x34, 0x34, 0x54, 0xfa, 0xff,
0x38, 0xd3, 0x99, 0xc4, 0xbe, 0x6a, 0xd8, 0xdc,
0xa0, 0x01, 0x69, 0xcd, 0x0d, 0x09, 0xee, 0x05,
0x1a, 0x90, 0xd6, 0xdc, 0xc7, 0xf7, 0x42, 0x82,
0x3b, 0x43, 0x87, 0x4e, 0xa9, 0x94, 0x71, 0x3e,
0xf7, 0x2f, 0x80, 0x14, 0x0a, 0x6c, 0x75, 0x8d,
0xac, 0x01, 0x02, 0x48, 0x21, 0xb9, 0x38, 0xd7,
0xd6, 0xc8, 0x1a, 0x20, 0x80, 0x14, 0x2a, 0x8a,
0x6d, 0x4d, 0x8c, 0x54, 0x8d, 0x1d, 0xb3, 0x8a,
0x2c, 0x13, 0x53, 0x7d, 0xb3, 0x2c, 0x3c, 0x75,
0x83, 0x93, 0x0b, 0x03, 0x55, 0x8d, 0x1d, 0x8d,
0x54, 0x8d, 0x1d, 0x0d, 0x4d, 0x0c, 0x2c, 0x4c,
0xcd, 0x2c, 0x4c, 0xcc, 0x0d, 0x55, 0x8d, 0x1d,
0x4d, 0x0d, 0x8d, 0x8c, 0xad, 0x01, 0x02, 0x48,
0xa1, 0xd8, 0xd6, 0x31, 0xd1, 0xc4, 0xa8, 0xa4,
0x28, 0xa2, 0xd2, 0xdd, 0x33, 0xb0, 0x3c, 0xaa,
0x2c, 0xd0, 0xdf, 0x3b, 0xbc, 0x5c, 0xcf, 0x29,
0xc4, 0x32, 0xb0, 0x32, 0xc2, 0x1a, 0x20, 0x80,
0x14, 0xd2, 0x8a, 0x6c, 0x0d, 0x4c, 0xfc, 0x3c,
0x93, 0x7c, 0x9c, 0x0a, 0x9d, 0x42, 0x83, 0x53,
0xbc, 0x7d, 0xfc, 0x32, 0x83, 0xf5, 0x1c, 0xc3,
0x23, 0xfc, 0x33, 0xf3, 0x92, 0x53, 0x33, 0x0c,
0x82, 0xcd, 0x4a, 0x3d, 0x92, 0x8b, 0x83, 0x53,
0x12, 0x43, 0xe2, 0xbd, 0x4d, 0xe3, 0x33, 0xdd,
0xb3, 0xf5, 0x9c, 0x82, 0x8b, 0x4c, 0x72, 0x13,
0xf5, 0x5c, 0x0b, 0xf5, 0xdc, 0x22, 0x73, 0xf5,
0x0c, 0xf4, 0x1c, 0xc3, 0x43, 0xbd, 0x22, 0x1d,
0x5d, 0x52, 0xac, 0x01, 0x02, 0x48, 0x21, 0x39,
0xbe, 0xb4, 0x38, 0xb5, 0xc8, 0xd6, 0xd4, 0xc2,
0xc0, 0xd4, 0xdc, 0xd4, 0xcc, 0xd0, 0xd0, 0x1a,
0x20, 0x80, 0x14, 0x12, 0x93, 0x4b, 0x6c, 0x0d,
0x4d, 0x4c, 0x4c, 0x8d, 0x2d, 0x4c, 0x0d, 0x4d,
0xcc, 0xcc, 0x0d, 0x2c, 0x54, 0x8d, 0xdc, 0x4c,
0xac, 0x01, 0x02, 0x10, 0x04, 0x07, 0x37, 0x08,
0xc3, 0x30, 0x00, 0x00, 0x57, 0x61, 0x84, 0xd8,
0xb1, 0xc1, 0x79, 0xf0, 0xa8, 0x54, 0x7b, 0x8d,
0x8a, 0x16, 0x3f, 0x90, 0xd2, 0x08, 0x25, 0x0e,
0x5d, 0x9f, 0xbb, 0xdb, 0xb7, 0xfb, 0xf0, 0x76,
0xf8, 0x53, 0xd7, 0x9f, 0x65, 0x8d, 0xcf, 0xe9,
0x06, 0x44, 0x9c, 0x85, 0x11, 0x8b, 0xce, 0xe1,
0xdd, 0x16, 0x64, 0x49, 0xfc, 0xe0, 0x3b, 0xc0,
0x82, 0x3a, 0xe2, 0x15, 0x6e, 0xeb, 0xd8, 0xd1,
0x80, 0x88, 0xb3, 0x50, 0x4a, 0x52, 0x72, 0xd1,
0x40, 0xdb, 0x78, 0xdf, 0xf8, 0xad, 0xf5, 0x44,
0x6b, 0xb3, 0x56, 0x9d, 0x47, 0xa0, 0x01, 0x11,
0x67, 0xa1, 0x94, 0xa4, 0x10, 0x68, 0x74, 0x6b,
0xb3, 0x56, 0x8d, 0xcb, 0x00, 0xff, 0x04, 0xc1,
0x41, 0x0e, 0x80, 0x30, 0x08, 0x04, 0xc0, 0x2f,
0x95, 0xc2, 0x42, 0x37, 0x1e, 0xb1, 0xf4, 0x19,
0xc6, 0x98, 0x18, 0x8f, 0x1e, 0xfc, 0x7f, 0x9c,
0xa1, 0x36, 0xd0, 0xdd, 0xe7, 0xf9, 0x95, 0x98,
0x41, 0x87, 0x19, 0xe9, 0x9d, 0xcb, 0x0c, 0x3a,
0xd0, 0x3b, 0x23, 0x34, 0xe7, 0xf5, 0xd4, 0xfe,
0x1e, 0xb8, 0x31, 0x1a, 0x02, 0x2e, 0x52, 0x02,
0x32, 0x73, 0xfb, 0x05, 0x90, 0x42, 0x79, 0x8a,
0xad, 0x91, 0x85, 0x89, 0x49, 0x85, 0xa1, 0xa9,
0xa9, 0x05, 0x40, 0x00, 0x01, 0x00, 0x00, 0x00,
0xff, 0xff,
};

TEST(SPDYCodecTest, SynStreamBoundaries) {
  for (int i = -1; i < int(sizeof(synStream)); i++) {
    SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3_1);
    FakeHTTPCodecCallback callbacks;
    size_t unconsumed =
      parseSPDY(&codec, synStream, sizeof(synStream), i, callbacks);
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
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3_1);
  codec.setMaxFrameLength(500);
  FakeHTTPCodecCallback callbacks;
  size_t unconsumed = parseSPDY(&codec, synStream, sizeof(synStream), -1,
                                callbacks);
  EXPECT_EQ(unconsumed, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
  EXPECT_EQ(callbacks.streamErrors, 1);
}

TEST(SPDYCodecTest, FrameUncompressedTooLarge) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3_1);
  codec.setMaxUncompressedHeaders(600);
  FakeHTTPCodecCallback callbacks;
  size_t unconsumed = parseSPDY(&codec, synStream, sizeof(synStream), -1,
                                callbacks);
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
  size_t unconsumed = parseSPDY(&codec, shortSynStream,
                                sizeof(shortSynStream), -1, callbacks);
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
  size_t unconsumed = parseSPDY(&codec, spdy3UnknownCtlFrame,
                                sizeof(spdy3UnknownCtlFrame), -1, callbacks);
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

#define permuteTest(f) copyForMe(f, SPDYCodec, SPDY3);              \
                       copyForMe(f, SPDYCodec, SPDY3_1);

/**
 * Returns a SPDY frame with the specified version
 */
unique_ptr<folly::IOBuf> getVersionedSpdyFrame(const uint8_t* bytes,
                                               size_t len,
                                               uint8_t version) {
  auto frame = folly::IOBuf::copyBuffer(bytes, len);
  uint8_t* data = frame->writableData();
  data[1] = version; /* Set the version */
  return frame;
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
 * generated by using the v3 synStream firefox frame specified above and
 * changing the version field to 2. The dictionary used for the zlib compression
 * will be different and the session will be rejected.
 */
TEST(SPDYCodecTest, SynStreamWrongVersion) {
  SPDYCodec codec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3);
  FakeHTTPCodecCallback callbacks;
  auto frame = getVersionedSpdyFrame(synStream, sizeof(synStream), 2);
  size_t unconsumed = parseSPDY(&codec, frame->data(), frame->length(), -1,
                                callbacks);
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
  SPDYCodec spdy3(TransportDirection::UPSTREAM,
                  SPDYVersion::SPDY3);
  SPDYCodec spdy3_1(TransportDirection::UPSTREAM,
                    SPDYVersion::SPDY3_1);
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


uint8_t multiple_path_headers[] =
    {0x80, 0x03, 0x00, 0x01, 0x01, 0x00, 0x01, 0x13, 0x00, 0x00, 0x00, 0x01,
     0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x38, 0x30, 0xe3, 0xc6, 0xa7, 0xc2,
     0x00, 0xf9, 0x00, 0x06, 0xff, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
     0x05, 0x3a, 0x68, 0x6f, 0x73, 0x74, 0x00, 0x00, 0x00, 0x10, 0x77, 0x77,
     0x77, 0x2e, 0x66, 0x61, 0x63, 0x65, 0x62, 0x6f, 0x6f, 0x6b, 0x2e, 0x63,
     0x6f, 0x6d, 0x00, 0x00, 0x00, 0x07, 0x3a, 0x6d, 0x65, 0x74, 0x68, 0x6f,
     0x64, 0x00, 0x00, 0x00, 0x03, 0x47, 0x45, 0x54, 0x00, 0x00, 0x00, 0x05,
     0x3a, 0x70, 0x61, 0x74, 0x68, 0x00, 0x00, 0x00, 0x35, 0x2f, 0x65, 0x6e,
     0x63, 0x72, 0x79, 0x70, 0x74, 0x65, 0x64, 0x72, 0x65, 0x71, 0x75, 0x65,
     0x73, 0x74, 0x00, 0x2a, 0x23, 0x2f, 0x69, 0x6e, 0x74, 0x65, 0x72, 0x6e,
     0x2f, 0x61, 0x67, 0x65, 0x6e, 0x74, 0x2f, 0x73, 0x69, 0x74, 0x65, 0x5f,
     0x76, 0x61, 0x72, 0x69, 0x61, 0x62, 0x6c, 0x65, 0x73, 0x2e, 0x70, 0x68,
     0x70, 0x3f, 0x00, 0x00, 0x00, 0x07, 0x3a, 0x73, 0x63, 0x68, 0x65, 0x6d,
     0x65, 0x00, 0x00, 0x00, 0x05, 0x68, 0x74, 0x74, 0x70, 0x73, 0x00, 0x00,
     0x00, 0x08, 0x3a, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x00, 0x00,
     0x00, 0x08, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x31, 0x00, 0x00,
     0x00, 0x06, 0x61, 0x63, 0x63, 0x65, 0x70, 0x74, 0x00, 0x00, 0x00, 0x03,
     0x2a, 0x2f, 0x2a, 0x00, 0x00, 0x00, 0x0f, 0x61, 0x63, 0x63, 0x65, 0x70,
     0x74, 0x2d, 0x65, 0x6e, 0x63, 0x6f, 0x64, 0x69, 0x6e, 0x67, 0x00, 0x00,
     0x00, 0x0d, 0x67, 0x7a, 0x69, 0x70, 0x2c, 0x20, 0x64, 0x65, 0x66, 0x6c,
     0x61, 0x74, 0x65, 0x00, 0x00, 0x00, 0x0a, 0x75, 0x73, 0x65, 0x72, 0x2d,
     0x61, 0x67, 0x65, 0x6e, 0x74, 0x00, 0x00, 0x00, 0x11, 0x73, 0x70, 0x64,
     0x79, 0x6c, 0x61, 0x79, 0x2f, 0x31, 0x2e, 0x33, 0x2e, 0x33, 0x2d, 0x44,
     0x45, 0x56, 0x00, 0x00, 0x00, 0xff, 0xff, 0x80, 0x03, 0x00, 0x07, 0x00,
     0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Test multiple paths in a message
TEST(SPDYCodecTest, MultiplePaths) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM, SPDYVersion::SPDY3);
  auto frame = getVersionedSpdyFrame(multiple_path_headers,
     sizeof(multiple_path_headers), ingressCodec.getVersion());
  ingressCodec.setCallback(&callbacks);
  ingressCodec.onIngress(*frame);
  EXPECT_EQ(callbacks.streamErrors, 1);
  EXPECT_EQ(callbacks.sessionErrors, 0);
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

TEST(SPDYCodecTest, ServerPushWithStatus) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec egressCodec(TransportDirection::DOWNSTREAM,
                        SPDYVersion::SPDY3);
  SPDYCodec ingressCodec(TransportDirection::UPSTREAM,
                         SPDYVersion::SPDY3);
  ingressCodec.setCallback(&callbacks);

  HTTPMessage push;
  push.getHeaders().set("HOST", "www.foo.com");
  push.setURL("https://www.foo.com/");
  push.setPushStatusCode(200);
  auto syn = getSynStream(egressCodec, 2, push, 1);
  ingressCodec.onIngress(*syn);
  EXPECT_EQ(callbacks.messageBegin, 1);
  EXPECT_EQ(callbacks.headersComplete, 1);
  EXPECT_EQ(callbacks.messageComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 0);
  EXPECT_EQ(callbacks.assocStreamId, 1);
  EXPECT_EQ(callbacks.msg->getPushStatusCode(), 200);
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
                        SPDYVersion::SPDY3_1);
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3_1);
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

TEST(SPDYCodecTest, StreamIdOverflow) {
  SPDYCodec codec(TransportDirection::UPSTREAM,
                  SPDYVersion::SPDY3_1);

  HTTPCodec::StreamID streamId;
  codec.setNextEgressStreamId(std::numeric_limits<int32_t>::max() - 10);
  while (codec.isReusable()) {
    streamId = codec.createStream();
  }
  EXPECT_EQ(streamId, std::numeric_limits<int32_t>::max() - 2);
}

const uint8_t kBufBadNVBlock[] = {
 0x80, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1c,
 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x78, 0xbb, 0xe3, 0xc6, 0xa7, 0xc2,
 0x02, 0xa6, 0x23, 0xc6, 0xff, 0x40, 0x00, 0x00,
 0x00, 0x00, 0xff, 0xff
};

/**
 * Test case where nv item length is greater than total frame length
 */
TEST(SPDYCodecTest, BadNVBlock) {
  FakeHTTPCodecCallback callbacks;
  SPDYCodec ingressCodec(TransportDirection::DOWNSTREAM,
                         SPDYVersion::SPDY3_1);

  auto testBuf = IOBuf::copyBuffer(kBufBadNVBlock, sizeof(kBufBadNVBlock));
  ingressCodec.setCallback(&callbacks);
  ingressCodec.onIngress(*testBuf);
  EXPECT_EQ(callbacks.headersComplete, 0);
  EXPECT_EQ(callbacks.streamErrors, 0);
  EXPECT_EQ(callbacks.sessionErrors, 1);
}

class SPDYCodecTestF : public HTTPParallelCodecTest {

 public:
  SPDYCodecTestF()
      : HTTPParallelCodecTest(upstreamCodec_, downstreamCodec_) {}

 protected:
  SPDYCodec upstreamCodec_{TransportDirection::UPSTREAM, SPDYVersion::SPDY3_1};
  SPDYCodec downstreamCodec_{TransportDirection::DOWNSTREAM,
      SPDYVersion::SPDY3_1};
};

TEST_F(SPDYCodecTestF, GoawayHandling) {
  // send request
  HTTPMessage req = getGetRequest();
  HTTPHeaderSize size;
  size.uncompressed = size.compressed = 0;
  upstreamCodec_.generateHeader(output_, 1, req, 0, true, &size);
  EXPECT_GT(size.uncompressed, 0);
  parse();
  callbacks_.expectMessage(true, 2, "/");
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
  req.setPushStatusCode(200);
  req.getHeaders().add("foomonkey", "george");
  downstreamCodec_.generateHeader(output_, 2, req, 1, false, &size);
  EXPECT_GT(size.uncompressed, 0);
  // window update for push doesn't make any sense, but whatever
  downstreamCodec_.generateWindowUpdate(output_, 2, 100);
  downstreamCodec_.generateBody(output_, 2, makeBuf(10), boost::none, false);

  // tell the upstream no pushing, and parse the first batch
  IOBufQueue dummy{IOBufQueue::cacheChainLength()};
  upstreamCodec_.generateGoaway(dummy, 0, ErrorCode::NO_ERROR);
  parseUpstream();

  downstreamCodec_.generatePriority(output_, 2,
                                    HTTPMessage::HTTPPriority(0, true, 1));
  downstreamCodec_.generateEOM(output_, 2);
  downstreamCodec_.generateRstStream(output_, 2, ErrorCode::CANCEL);

  // send a response that will be accepted, headers should be ok
  HTTPMessage resp;
  resp.setStatusCode(200);
  downstreamCodec_.generateHeader(output_, 1, resp, 0, true, &size);
  EXPECT_GT(size.uncompressed, 0);

  // parse the remainder
  parseUpstream();
  callbacks_.expectMessage(true, 1, 200);
}
