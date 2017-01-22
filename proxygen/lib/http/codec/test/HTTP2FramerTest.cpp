/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/test/HTTP2FramerTest.h>

#include <folly/Random.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>
#include <random>

#include <proxygen/lib/http/codec/HTTP2Framer.h>

using namespace folly::io;
using namespace folly;
using namespace proxygen::http2;
using namespace proxygen;
using namespace std;

void writeFrameHeaderManual(IOBufQueue& queue, uint32_t length, uint8_t type,
                            uint8_t flags, uint32_t stream) {
  QueueAppender appender(&queue, kFrameHeaderSize);
  uint32_t lengthAndType = length << 8 | type;
  appender.writeBE<uint32_t>(lengthAndType);
  appender.writeBE<uint8_t>(flags);
  appender.writeBE<uint32_t>(stream);
}

class HTTP2FramerTest : public testing::Test {
 public:
  HTTP2FramerTest() {}

  template<typename... Args>
  void parse(ErrorCode (*parseFn)(Cursor& cursor, FrameHeader, Args...),
             FrameHeader& outHeader, Args&&... outArgs) {
    parse(queue_.front(), parseFn, outHeader, std::forward<Args>(outArgs)...);
  }

  template<typename... Args>
  void parse(const IOBuf* data,
             ErrorCode (*parseFn)(Cursor& cursor, FrameHeader, Args...),
             FrameHeader& outHeader, Args&&... outArgs) {
    Cursor cursor(data);
    auto ret1 = parseFrameHeader(cursor, outHeader);
    ASSERT_EQ(ret1, ErrorCode::NO_ERROR);
    auto ret2 = (*parseFn)(cursor, outHeader, std::forward<Args>(outArgs)...);
    ASSERT_EQ(ret2, ErrorCode::NO_ERROR);
  }

  void dataFrameTest(uint32_t dataLen, boost::optional<uint8_t> padLen) {
    auto body = makeBuf(dataLen);
    dataFrameTest(body.get(), dataLen, padLen);
  }

  void dataFrameTest(IOBuf* body, uint32_t dataLen,
                     boost::optional<uint8_t> padLen) {
    uint32_t frameLen = uint32_t(dataLen);
    if (padLen) {
      frameLen += 1 + padLen.get();
    }
    if (frameLen > kMaxFramePayloadLength) {
      EXPECT_DEATH_NO_CORE(writeData(queue_, body->clone(), 1, padLen,
                                     false), ".*");
    } else {
      writeData(queue_, body->clone(), 1, padLen, false);

      FrameHeader outHeader;
      std::unique_ptr<IOBuf> outBuf;
      uint16_t padding = 0;
      parse(&parseData, outHeader, outBuf, padding);

      EXPECT_EQ(outBuf->moveToFbString(), body->moveToFbString());
      EXPECT_EQ(padding, padLen ? (*padLen + 1) : 0);
    }
    queue_.move(); // reset everything
  }

  IOBufQueue queue_{IOBufQueue::cacheChainLength()};
};

TEST_F(HTTP2FramerTest, WriteAndParseSmallDataNoPad) {
  dataFrameTest(20, kNoPadding);
}

TEST_F(HTTP2FramerTest, WriteAndParseSmallDataZeroPad) {
  dataFrameTest(20, Padding(0));
}

TEST_F(HTTP2FramerTest, WriteAndParseSmallDataSmallPad) {
  dataFrameTest(20, Padding(20));
}

TEST_F(HTTP2FramerTest, ManyDataFrameSizes) {
  dataFrameTest(0, kNoPadding);
  for (uint32_t dataLen = 1; dataLen < kMaxFramePayloadLength; dataLen <<= 1) {
    auto body = makeBuf(dataLen);
    dataFrameTest(body.get(), dataLen, Padding(0));
    dataFrameTest(0, Padding(dataLen));
    for (uint8_t padding = 1; padding != 0; padding <<= 1) {
      dataFrameTest(body.get(), dataLen, Padding(padding));
    }
  }
}

TEST_F(HTTP2FramerTest, BadStreamData) {
  writeFrameHeaderManual(queue_, 0,
                         static_cast<uint8_t>(FrameType::DATA),
                         0, 0);
  FrameHeader outHeader;
  std::unique_ptr<IOBuf> outBuf;
  uint16_t padding = 0;
  Cursor cursor(queue_.front());
  EXPECT_EQ(parseFrameHeader(cursor, outHeader), ErrorCode::NO_ERROR);
  EXPECT_EQ(parseData(cursor, outHeader, outBuf, padding),
            ErrorCode::PROTOCOL_ERROR);
}

TEST_F(HTTP2FramerTest, BadStreamSettings) {
  writeFrameHeaderManual(queue_, 0,
                         static_cast<uint8_t>(FrameType::SETTINGS),
                         ACK, 1);
  FrameHeader outHeader;
  std::deque<SettingPair> outSettings;
  Cursor cursor(queue_.front());
  EXPECT_EQ(parseFrameHeader(cursor, outHeader), ErrorCode::NO_ERROR);
  EXPECT_EQ(parseSettings(cursor, outHeader, outSettings),
            ErrorCode::PROTOCOL_ERROR);
}

TEST_F(HTTP2FramerTest, BadPad) {
  auto body = makeBuf(5);
  writeData(queue_, body->clone(), 1, Padding(5), false);
  queue_.trimEnd(5);
  queue_.append(string("abcde"));

  FrameHeader outHeader;
  std::unique_ptr<IOBuf> outBuf;
  uint16_t padding = 0;
  Cursor cursor(queue_.front());
  EXPECT_EQ(parseFrameHeader(cursor, outHeader), ErrorCode::NO_ERROR);
  EXPECT_EQ(parseData(cursor, outHeader, outBuf, padding),
            ErrorCode::PROTOCOL_ERROR);
}

TEST_F(HTTP2FramerTest, BadStreamId) {
  // We should crash on DBG builds if the stream id > 2^31 - 1
  EXPECT_DEATH_NO_CORE(writeRstStream(queue_,
                                      static_cast<uint32_t>(-1),
                                      ErrorCode::PROTOCOL_ERROR), ".*");
}

TEST_F(HTTP2FramerTest, RstStream) {
  writeRstStream(queue_, 1, ErrorCode::CANCEL);

  FrameHeader header;
  ErrorCode outCode;
  parse(&parseRstStream, header, outCode);

  ASSERT_EQ(FrameType::RST_STREAM, header.type);
  ASSERT_EQ(1, header.stream);
  ASSERT_EQ(0, header.flags);
  ASSERT_EQ(kFrameRstStreamSize, header.length);
  ASSERT_EQ(ErrorCode::CANCEL, outCode);
}

TEST_F(HTTP2FramerTest, Goaway) {
  writeGoaway(queue_, 0, ErrorCode::FLOW_CONTROL_ERROR);

  FrameHeader header;
  uint32_t lastStreamID;
  ErrorCode outCode;
  std::unique_ptr<IOBuf> outDebugData;
  parse(&parseGoaway, header, lastStreamID, outCode, outDebugData);

  ASSERT_EQ(FrameType::GOAWAY, header.type);
  ASSERT_EQ(0, header.stream);
  ASSERT_EQ(0, header.flags);
  ASSERT_EQ(kFrameGoawaySize, header.length);
  ASSERT_EQ(ErrorCode::FLOW_CONTROL_ERROR, outCode);
  ASSERT_EQ(0, lastStreamID);
  ASSERT_FALSE(outDebugData);
}

TEST_F(HTTP2FramerTest, GoawayDebugData) {
  string data("abcde");
  auto debugData = makeBuf(5);
  memcpy(debugData->writableData(), data.data(), data.length());
  writeGoaway(queue_, 0, ErrorCode::FLOW_CONTROL_ERROR, std::move(debugData));

  FrameHeader header;
  uint32_t lastStreamID;
  ErrorCode outCode;
  std::unique_ptr<IOBuf> outDebugData;
  parse(&parseGoaway, header, lastStreamID, outCode, outDebugData);

  ASSERT_EQ(FrameType::GOAWAY, header.type);
  ASSERT_EQ(0, header.stream);
  ASSERT_EQ(0, header.flags);
  ASSERT_EQ(kFrameGoawaySize + data.length(), header.length);
  ASSERT_EQ(ErrorCode::FLOW_CONTROL_ERROR, outCode);
  ASSERT_EQ(0, lastStreamID);
  ASSERT_EQ(outDebugData->computeChainDataLength(), data.length());
  EXPECT_EQ(outDebugData->moveToFbString(), data);
}

// An invalid error code. Used to test a bug where
// a macro expansion caused us to read the error code twice
TEST_F(HTTP2FramerTest, GoawayDoubleRead) {
    writeFrameHeaderManual(
      queue_,
      kFrameGoawaySize,
      static_cast<uint8_t>(FrameType::GOAWAY),
      0,
      0);

    QueueAppender appender(&queue_, kFrameGoawaySize);
    appender.writeBE<uint32_t>(0);
    // Here's the invalid value:
    appender.writeBE<uint32_t>(static_cast<uint32_t>(0xffffffff));

    uint32_t outLastStreamID;
    ErrorCode outCode;
    std::unique_ptr<IOBuf> outDebugData;
    FrameHeader outHeader;
    Cursor cursor(queue_.front());

    auto ret1 = parseFrameHeader(cursor, outHeader);
    ASSERT_EQ(ErrorCode::NO_ERROR, ret1);
    ASSERT_EQ(FrameType::GOAWAY, outHeader.type);
    auto ret2 = parseGoaway(cursor, outHeader, outLastStreamID,
                            outCode, outDebugData);
    ASSERT_EQ(ErrorCode::PROTOCOL_ERROR, ret2);
}

TEST_F(HTTP2FramerTest, Priority) {
  writePriority(queue_, 102, {0, false, 30});

  FrameHeader header;
  PriorityUpdate priority;
  parse(&parsePriority, header, priority);

  ASSERT_EQ(FrameType::PRIORITY, header.type);
  ASSERT_EQ(102, header.stream);
  ASSERT_EQ(0, header.flags);
  ASSERT_EQ(0, priority.streamDependency);
  ASSERT_FALSE(priority.exclusive);
  ASSERT_EQ(30, priority.weight);
}

TEST_F(HTTP2FramerTest, HeadersWithPaddingAndPriority) {
  auto body = makeBuf(500);
  writeHeaders(queue_, body->clone(), 1, {{0, true, 12}}, 200,
               false, false);

  FrameHeader header;
  boost::optional<PriorityUpdate> priority;
  std::unique_ptr<IOBuf> outBuf;
  parse(&parseHeaders, header, priority, outBuf);

  ASSERT_EQ(FrameType::HEADERS, header.type);
  ASSERT_EQ(1, header.stream);
  ASSERT_TRUE(PRIORITY & header.flags);
  ASSERT_FALSE(END_STREAM & header.flags);
  ASSERT_FALSE(END_HEADERS & header.flags);
  ASSERT_EQ(0, priority->streamDependency);
  ASSERT_TRUE(priority->exclusive);
  ASSERT_EQ(12, priority->weight);
  EXPECT_EQ(outBuf->moveToFbString(), body->moveToFbString());
}

TEST_F(HTTP2FramerTest, Ping) {
  uint64_t data = folly::Random::rand64();
  writePing(queue_, data, false);

  FrameHeader header;
  uint64_t outData;
  parse(&parsePing, header, outData);

  ASSERT_EQ(FrameType::PING, header.type);
  ASSERT_EQ(0, header.stream);
  ASSERT_EQ(0, header.flags);
  EXPECT_EQ(data, outData);
}

TEST_F(HTTP2FramerTest, WindowUpdate) {
  writeWindowUpdate(queue_, 33, 120);

  FrameHeader header;
  uint32_t amount;
  parse(&parseWindowUpdate, header, amount);

  ASSERT_EQ(FrameType::WINDOW_UPDATE, header.type);
  ASSERT_EQ(33, header.stream);
  ASSERT_EQ(0, header.flags);
  EXPECT_EQ(120, amount);
}

// TODO: auto generate this test for all frame types (except DATA)
TEST_F(HTTP2FramerTest, ShortWindowUpdate) {
  // length field is too short for frame type
  writeFrameHeaderManual(queue_, 0,
                         static_cast<uint8_t>(FrameType::WINDOW_UPDATE),
                         0, 0);

  Cursor cursor(queue_.front());
  FrameHeader header;
  uint32_t amount;
  auto ret1 = parseFrameHeader(cursor, header);
  auto ret2 = parseWindowUpdate(cursor, header, amount);
  ASSERT_EQ(ErrorCode::FRAME_SIZE_ERROR, ret2);
}

TEST_F(HTTP2FramerTest, Uint32MaxWindowUpdate) {
  const uint32_t uint31Max = 0x7fffffff;
  writeWindowUpdate(queue_, 33, 1); // we will overwrite the update size
  auto buf = queue_.move();
  buf->coalesce();
  // Set update size to unit32_t max
  for (unsigned i = 0; i < 4; ++i) {
    buf->writableData()[kFrameHeaderSize + i] = 0xff;
  }

  FrameHeader header;
  uint32_t amount;
  parse(buf.get(), &parseWindowUpdate, header, amount);

  ASSERT_EQ(FrameType::WINDOW_UPDATE, header.type);
  ASSERT_EQ(33, header.stream);
  ASSERT_EQ(0, header.flags);
  // We should ignore the top bit
  EXPECT_EQ(uint31Max, amount);
}

TEST_F(HTTP2FramerTest, AltSvc) {
  string protocol = "special-proto";
  string host = "special-host";
  string origin = "special-origin";
  writeAltSvc(queue_, 2, 150, 8080, protocol, host, origin);

  FrameHeader header;
  uint32_t outMaxAge;
  uint32_t outPort;
  string outProtocol;
  string outHost;
  string outOrigin;
  parse(&parseAltSvc, header, outMaxAge, outPort, outProtocol,
        outHost, outOrigin);

  ASSERT_EQ(FrameType::ALTSVC, header.type);
  ASSERT_EQ(2, header.stream);
  ASSERT_EQ(0, header.flags);
  EXPECT_EQ(150, outMaxAge);
  EXPECT_EQ(8080, outPort);
  EXPECT_EQ(protocol, outProtocol);
  EXPECT_EQ(host, outHost);
  EXPECT_EQ(origin, outOrigin);
}

TEST_F(HTTP2FramerTest, Settings) {
  const deque<SettingPair> settings = {{SettingsId::HEADER_TABLE_SIZE, 3},
                                       {SettingsId::MAX_CONCURRENT_STREAMS, 4}};
  writeSettings(queue_, settings);

  FrameHeader header;
  std::deque<SettingPair> outSettings;
  parse(&parseSettings, header, outSettings);

  ASSERT_EQ(settings.size() * 6, header.length);
  ASSERT_EQ(FrameType::SETTINGS, header.type);
  ASSERT_EQ(0, header.stream);
  ASSERT_EQ(0, header.flags);
  ASSERT_EQ(settings, outSettings);
}

TEST_F(HTTP2FramerTest, SettingsAck) {
  writeSettingsAck(queue_);

  Cursor cursor(queue_.front());
  FrameHeader header;
  auto ret = parseFrameHeader(cursor, header);
  ASSERT_EQ(ErrorCode::NO_ERROR, ret);

  ASSERT_EQ(0, header.length);
  ASSERT_EQ(FrameType::SETTINGS, header.type);
  ASSERT_EQ(0, header.stream);
  ASSERT_EQ(ACK, header.flags);
  std::deque<SettingPair> outSettings;
  EXPECT_EQ(parseSettings(cursor, header, outSettings), ErrorCode::NO_ERROR);
}

TEST_F(HTTP2FramerTest, BadSettingsAck) {
  writeSettingsAck(queue_);
  // Add an extra byte to the frame
  uint32_t lengthAndType = htonl(1 << 8 | uint8_t(FrameType::SETTINGS));
  memcpy(((IOBuf*)queue_.front())->writableData(), &lengthAndType, 4);
  queue_.append("o");

  Cursor cursor(queue_.front());
  FrameHeader header;
  auto ret = parseFrameHeader(cursor, header);
  ASSERT_EQ(ErrorCode::NO_ERROR, ret);

  ASSERT_EQ(FrameType::SETTINGS, header.type);
  ASSERT_EQ(0, header.stream);
  ASSERT_EQ(ACK, header.flags);
  std::deque<SettingPair> outSettings;
  EXPECT_EQ(parseSettings(cursor, header, outSettings),
            ErrorCode::FRAME_SIZE_ERROR);
}

TEST_F(HTTP2FramerTest, UnknownSetting) {
  const deque<SettingPair> settings = {{(proxygen::SettingsId)31337, 3},
                                       {(proxygen::SettingsId)0, 4}};
  writeSettings(queue_, settings);

  FrameHeader header;
  std::deque<SettingPair> outSettings;
  parse(&parseSettings, header, outSettings);

  ASSERT_EQ(settings.size() * 6, header.length);
  ASSERT_EQ(FrameType::SETTINGS, header.type);
  ASSERT_EQ(0, header.stream);
  ASSERT_EQ(0, header.flags);
  ASSERT_EQ(settings, outSettings);
}

TEST_F(HTTP2FramerTest, PushPromise) {
  auto body = makeBuf(500);
  writePushPromise(queue_, 21, 22, body->clone(), 255, true);

  FrameHeader header;
  uint32_t promisedStream;
  std::unique_ptr<IOBuf> outBuf;
  parse(&parsePushPromise, header, promisedStream, outBuf);

  ASSERT_EQ(FrameType::PUSH_PROMISE, header.type);
  ASSERT_EQ(21, header.stream);
  ASSERT_EQ(22, promisedStream);
  EXPECT_EQ(outBuf->moveToFbString(), body->moveToFbString());
}

TEST_F(HTTP2FramerTest, Continuation) {
  auto body = makeBuf(800);
  writeContinuation(queue_, 1, true, body->clone(), 2);

  FrameHeader header;
  std::unique_ptr<IOBuf> outBuf;
  parse(&parseContinuation, header, outBuf);

  ASSERT_EQ(FrameType::CONTINUATION, header.type);
  ASSERT_TRUE(END_HEADERS & header.flags);
  ASSERT_TRUE(PADDED & header.flags);
  ASSERT_EQ(1, header.stream);
  EXPECT_EQ(outBuf->moveToFbString(), body->moveToFbString());
}
