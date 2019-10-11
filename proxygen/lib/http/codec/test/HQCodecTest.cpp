/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/codec/HQControlCodec.h>
#include <proxygen/lib/http/codec/HQStreamCodec.h>
#include <proxygen/lib/http/codec/QPACKDecoderCodec.h>
#include <proxygen/lib/http/codec/QPACKEncoderCodec.h>
#include <proxygen/lib/http/codec/TransportDirection.h>
#include <proxygen/lib/http/codec/compress/test/TestStreamingCallback.h>
#include <proxygen/lib/http/codec/compress/test/TestUtil.h>
#include <proxygen/lib/http/codec/test/HQFramerTest.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>

using namespace folly;
using namespace proxygen;
using namespace proxygen::hq;
using namespace testing;

// a FakeHTTPCodecCallback that can
// be used as a unidirectional codec as well
class FakeHQHTTPCodecCallback
    : public FakeHTTPCodecCallback
    , public HQUnidirectionalCodec::Callback {
 public:
  // Delegate HQUnidirectionalCodec::Callback::onError
  // to FakeHTTPCodecCallback::onError
  void onError(HTTPCodec::StreamID streamId,
               const HTTPException& ex,
               bool newTxn) override {
    FakeHTTPCodecCallback::onError(streamId, ex, newTxn);
  }
};

enum class CodecType {
  UPSTREAM,
  DOWNSTREAM,
  CONTROL_UPSTREAM,
  CONTROL_DOWNSTREAM,
  H1Q_CONTROL_UPSTREAM,
  H1Q_CONTROL_DOWNSTREAM,
  PUSH,
};

// This is a template since for regular tests the fixture has to be derived from
// testing::Test and for parameterized tests it has to be derived from
// testing::TestWithParam<>
template <class T>
class HQCodecTestFixture : public T {
 public:
  void SetUp() override {
    makeCodecs(false);
    SetUpCodecs();
  }

  void SetUpCodecs() {
    callbacks_.setSessionStreamId(kSessionStreamId);
    downstreamCodec_->setCallback(&callbacks_);
    upstreamCodec_->setCallback(&callbacks_);
    upstreamControlCodec_.setCallback(&callbacks_);
    downstreamControlCodec_.setCallback(&callbacks_);
    upstreamH1qControlCodec_.setCallback(&callbacks_);
    downstreamH1qControlCodec_.setCallback(&callbacks_);
  }

  std::unique_ptr<folly::IOBuf> parse() {
    auto consumed = downstreamCodec_->onIngress(*queue_.front());
    queue_.trimStart(consumed);
    return queue_.move();
  }

  std::unique_ptr<folly::IOBuf> parseUpstream() {
    auto consumed = upstreamCodec_->onIngress(*queue_.front());
    queue_.trimStart(consumed);
    return queue_.move();
  }

  std::unique_ptr<folly::IOBuf> parseControl(CodecType type) {
    HQControlCodec* codec = nullptr;
    downstreamControlCodec_.setCallback(&callbacks_);
    upstreamH1qControlCodec_.setCallback(&callbacks_);
    downstreamH1qControlCodec_.setCallback(&callbacks_);

    switch (type) {
      case CodecType::CONTROL_UPSTREAM:
        codec = &upstreamControlCodec_;
        break;
      case CodecType::CONTROL_DOWNSTREAM:
        codec = &downstreamControlCodec_;
        break;
      case CodecType::H1Q_CONTROL_UPSTREAM:
        codec = &upstreamH1qControlCodec_;
        break;
      case CodecType::H1Q_CONTROL_DOWNSTREAM:
        codec = &downstreamH1qControlCodec_;
        break;
      default:
        LOG(FATAL) << "Unknown Control Codec type";
        break;
    }
    return codec->onUnidirectionalIngress(queueCtrl_.move());
  }

  void qpackTest(bool blocked);

  size_t addAndCheckSimpleHeaders() {
    std::array<uint8_t, 6> simpleReq{0x00, 0x00, 0xC0, 0xC1, 0xD1, 0xD7};
    writeFrameHeaderManual(
        queue_, static_cast<uint64_t>(FrameType::HEADERS), simpleReq.size());
    queue_.append(simpleReq.data(), simpleReq.size());
    size_t n = queue_.chainLength();
    parse();
    EXPECT_EQ(callbacks_.headerFrames, 1);
    EXPECT_EQ(callbacks_.headersComplete, 1);
    EXPECT_EQ(callbacks_.bodyCalls, 0);
    EXPECT_EQ(callbacks_.bodyLength, 0);
    return n;
  }

  void makeCodecs(bool isTransportPartiallyReliable) {
    upstreamCodec_ = std::make_unique<HQStreamCodec>(
        streamId_,
        TransportDirection::UPSTREAM,
        qpackUpstream_,
        qpackUpEncoderWriteBuf_,
        qpackUpDecoderWriteBuf_,
        [] { return std::numeric_limits<uint64_t>::max(); },
        egressSettings_,
        ingressSettings_,
        isTransportPartiallyReliable);
    downstreamCodec_ = std::make_unique<HQStreamCodec>(
        streamId_,
        TransportDirection::DOWNSTREAM,
        qpackDownstream_,
        qpackDownEncoderWriteBuf_,
        qpackDownDecoderWriteBuf_,
        [] { return std::numeric_limits<uint64_t>::max(); },
        egressSettings_,
        ingressSettings_,
        isTransportPartiallyReliable);
  }

 protected:
  FakeHQHTTPCodecCallback callbacks_;
  HTTPSettings egressSettings_;
  HTTPSettings ingressSettings_;
  HQControlCodec upstreamControlCodec_{0x1111,
                                       TransportDirection::UPSTREAM,
                                       StreamDirection::INGRESS,
                                       ingressSettings_,
                                       hq::UnidirectionalStreamType::CONTROL};
  HQControlCodec downstreamControlCodec_{0x2222,
                                         TransportDirection::DOWNSTREAM,
                                         StreamDirection::INGRESS,
                                         egressSettings_,
                                         hq::UnidirectionalStreamType::CONTROL};
  HQControlCodec upstreamH1qControlCodec_{
      0x1111,
      TransportDirection::UPSTREAM,
      StreamDirection::INGRESS,
      ingressSettings_,
      hq::UnidirectionalStreamType::H1Q_CONTROL};
  HQControlCodec downstreamH1qControlCodec_{
      0x2222,
      TransportDirection::DOWNSTREAM,
      StreamDirection::INGRESS,
      egressSettings_,
      hq::UnidirectionalStreamType::H1Q_CONTROL};
  QPACKCodec qpackUpstream_;
  QPACKCodec qpackDownstream_;
  QPACKEncoderCodec qpackEncoderCodec_{qpackDownstream_, callbacks_};
  QPACKDecoderCodec qpackDecoderCodec_{qpackUpstream_, callbacks_};
  HTTPCodec::StreamID streamId_{0x1234};
  IOBufQueue qpackUpEncoderWriteBuf_{IOBufQueue::cacheChainLength()};
  IOBufQueue qpackUpDecoderWriteBuf_{IOBufQueue::cacheChainLength()};
  IOBufQueue qpackDownEncoderWriteBuf_{IOBufQueue::cacheChainLength()};
  IOBufQueue qpackDownDecoderWriteBuf_{IOBufQueue::cacheChainLength()};
  std::unique_ptr<HQStreamCodec> upstreamCodec_{nullptr};
  std::unique_ptr<HQStreamCodec> downstreamCodec_{nullptr};
  IOBufQueue queue_{IOBufQueue::cacheChainLength()};
  IOBufQueue queueCtrl_{IOBufQueue::cacheChainLength()};
};

class HQCodecTest : public HQCodecTestFixture<Test> {};

class HQPRCodecTest : public HQCodecTest {
  void SetUp() override {
    makeCodecs(true);
    SetUpCodecs();
  }
};

TEST_F(HQCodecTest, DataFrame) {
  auto data = makeBuf(500);
  writeFrameHeaderManual(
      queue_, static_cast<uint64_t>(FrameType::DATA), data->length());
  queue_.append(data->clone());
  parse();
  EXPECT_EQ(callbacks_.headerFrames, 1);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, data->length());
}

TEST_F(HQPRCodecTest, DataFrameZeroLength) {
  const auto& ingressPrBodyTracker =
      downstreamCodec_->getIngressPrBodyTracker();

  auto data = makeBuf(500);
  size_t n =
      writeFrameHeaderManual(queue_, static_cast<uint8_t>(FrameType::DATA), 0);
  queue_.append(data->clone());
  parse();

  EXPECT_EQ(callbacks_.headerFrames, 1);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, data->length());

  auto bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, n);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(), 500);

  // Write again.
  queue_.append(data->clone());
  parse();
  EXPECT_EQ(callbacks_.headerFrames, 1);
  EXPECT_EQ(callbacks_.bodyCalls, 2);
  EXPECT_EQ(callbacks_.bodyLength, data->length() * 2);

  bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, n);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(), 1000);
}

TEST_F(HQPRCodecTest, DataFrameZeroLengthWithHeaders) {
  const auto& ingressPrBodyTracker =
      downstreamCodec_->getIngressPrBodyTracker();
  size_t n = addAndCheckSimpleHeaders();

  auto data = makeBuf(500);
  n += writeFrameHeaderManual(queue_, static_cast<uint8_t>(FrameType::DATA), 0);
  queue_.append(data->clone());
  parse();
  EXPECT_EQ(callbacks_.headerFrames, 2);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, data->length());

  auto bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, n);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(), 500);

  // Write again.
  queue_.append(data->clone());
  parse();
  EXPECT_EQ(callbacks_.headerFrames, 2);
  EXPECT_EQ(callbacks_.bodyCalls, 2);
  EXPECT_EQ(callbacks_.bodyLength, data->length() * 2);

  bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, n);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(), 1000);
}

TEST_F(HQPRCodecTest, TestOnIngressBodyPeek) {
  const auto& ingressPrBodyTracker =
      downstreamCodec_->getIngressPrBodyTracker();

  size_t n = addAndCheckSimpleHeaders();

  auto res = downstreamCodec_->onIngressDataAvailable(42);
  EXPECT_TRUE(res.hasError());
  EXPECT_EQ(res.error(), UnframedBodyOffsetTrackerError::NO_ERROR);

  auto data = makeBuf(500);
  n += writeFrameHeaderManual(queue_, static_cast<uint8_t>(FrameType::DATA), 0);
  queue_.append(data->clone());
  parse();
  EXPECT_EQ(callbacks_.headerFrames, 2);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, data->length());

  auto bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, n);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(), 500);

  res = downstreamCodec_->onIngressDataAvailable(42);
  EXPECT_FALSE(res.hasError());
  EXPECT_EQ(*res, 42 - n);

  bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, n);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(), 500);
}

TEST_F(HQPRCodecTest, TestOnIngressBodyPeekBadOffset) {
  const auto& ingressPrBodyTracker =
      downstreamCodec_->getIngressPrBodyTracker();
  size_t n = addAndCheckSimpleHeaders();

  auto data = makeBuf(500);
  n += writeFrameHeaderManual(queue_, static_cast<uint8_t>(FrameType::DATA), 0);
  queue_.append(data->clone());
  parse();
  EXPECT_EQ(callbacks_.headerFrames, 2);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, data->length());

  auto bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, n);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(), 500);

  // 1 is before body stream offset, so shouldn't trigger the callback.
  auto res = downstreamCodec_->onIngressDataAvailable(1);
  EXPECT_TRUE(res.hasError());

  bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, n);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(), 500);
}

/*
TEST_F(HQPRCodecTest, TestOnIngressBodyExpiredBeforeHeaders) {
  const auto& ingressPrBodyTracker = upstreamCodec_->getIngressPrBodyTracker();
  auto res = upstreamCodec_->onIngressDataExpired(42);
  EXPECT_TRUE(res.hasError());
  EXPECT_EQ(res.error(), UnframedBodyOffsetTrackerError::NO_ERROR);
  EXPECT_FALSE(ingressPrBodyTracker.bodyStarted());
}

TEST_F(HQPRCodecTest, TestOnIngressBodyExpiredBadstreamOffset) {
  const auto& ingressPrBodyTracker = upstreamCodec_->getIngressPrBodyTracker();
  HTTPMessage resp = getResponse(200, 500);
  auto streamId = upstreamCodec_->createStream();
  downstreamCodec_->generateHeader(queue_, streamId, resp, false, nullptr);
  parseUpstream();
  EXPECT_FALSE(ingressPrBodyTracker.bodyStarted());

  // 1 is before body stream offset, so shouldn't trigger the callback.
  auto res = upstreamCodec_->onIngressDataExpired(1);
  EXPECT_TRUE(res.hasError());
  EXPECT_EQ(res.error(), UnframedBodyOffsetTrackerError::INVALID_OFFSET);
  EXPECT_FALSE(ingressPrBodyTracker.bodyStarted());
}
*/

TEST_F(HQPRCodecTest, TestSplitPrEnabled) {
  const auto& ingressPrBodyTracker = upstreamCodec_->getIngressPrBodyTracker();
  HTTPMessage resp = getResponse(200, 500);
  resp.setPartiallyReliable();
  auto streamId = upstreamCodec_->createStream();
  uint64_t bodyStreamOffset = queue_.chainLength();
  downstreamCodec_->generateHeader(queue_, streamId, resp, false, nullptr);
  bodyStreamOffset = queue_.chainLength() - bodyStreamOffset;
  parseUpstream();
  EXPECT_TRUE(ingressPrBodyTracker.bodyStarted());

  // For upstream, only ingress should be PR-enabled.
  EXPECT_TRUE(upstreamCodec_->isIngressPartiallyRealible());
  EXPECT_FALSE(upstreamCodec_->isEgressPartiallyRealible());
  // The other way around for downstream.
  EXPECT_FALSE(downstreamCodec_->isIngressPartiallyRealible());
  EXPECT_TRUE(downstreamCodec_->isEgressPartiallyRealible());
}

TEST_F(HQPRCodecTest, TestOnIngressBodyExpiredStartWithSkip) {
  const auto& ingressPrBodyTracker = upstreamCodec_->getIngressPrBodyTracker();
  HTTPMessage resp = getResponse(200, 500);
  resp.setPartiallyReliable();
  auto streamId = upstreamCodec_->createStream();
  uint64_t bodyStreamOffset = queue_.chainLength();
  downstreamCodec_->generateHeader(queue_, streamId, resp, false, nullptr);
  bodyStreamOffset = queue_.chainLength() - bodyStreamOffset;
  parseUpstream();
  EXPECT_TRUE(ingressPrBodyTracker.bodyStarted());

  uint64_t testStreamOffset = 73;
  uint64_t expectedBodyAppOffset = testStreamOffset - bodyStreamOffset;

  auto res = upstreamCodec_->onIngressDataExpired(testStreamOffset);
  EXPECT_FALSE(res.hasError());
  EXPECT_EQ(*res, expectedBodyAppOffset);

  auto bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, bodyStreamOffset);

  auto data = makeBuf(500);
  uint64_t totalBodyBytesProcessed = expectedBodyAppOffset + 500;
  queue_.append(data->clone());
  parseUpstream();
  EXPECT_EQ(callbacks_.headerFrames, 2);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(),
            totalBodyBytesProcessed);

  testStreamOffset = 673;
  expectedBodyAppOffset = testStreamOffset - bodyStreamOffset;

  res = upstreamCodec_->onIngressDataExpired(testStreamOffset);
  EXPECT_FALSE(res.hasError());
  EXPECT_EQ(*res, expectedBodyAppOffset);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(),
            expectedBodyAppOffset);
}

TEST_F(HQPRCodecTest, TestOnIngressBodyExpiredStartWithBody) {
  const auto& ingressPrBodyTracker = upstreamCodec_->getIngressPrBodyTracker();
  HTTPMessage resp = getResponse(200, 500);
  auto streamId = upstreamCodec_->createStream();
  uint64_t bodyStreamOffset = queue_.chainLength();
  downstreamCodec_->generateHeader(queue_, streamId, resp, false, nullptr);
  writeFrameHeaderManual(queue_, static_cast<uint64_t>(FrameType::DATA), 0);
  bodyStreamOffset = queue_.chainLength() - bodyStreamOffset;
  parseUpstream();
  EXPECT_TRUE(ingressPrBodyTracker.bodyStarted());

  auto data = makeBuf(500);
  uint64_t expectedBodyBytesProcessed = 500;
  queue_.append(data->clone());
  parseUpstream();

  EXPECT_EQ(callbacks_.headerFrames, 2);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, expectedBodyBytesProcessed);
  auto bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, bodyStreamOffset);

  uint64_t testStreamOffset = 927;
  uint64_t expectedBodyAppOffset = testStreamOffset - bodyStreamOffset;
  auto res = upstreamCodec_->onIngressDataExpired(testStreamOffset);
  EXPECT_FALSE(res.hasError());
  EXPECT_EQ(*res, expectedBodyAppOffset);

  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(),
            expectedBodyAppOffset);
}

TEST_F(HQPRCodecTest, TestOnIngressBodyRejectedBeforeHeaders) {
  const auto& egressPrBodyTracker = downstreamCodec_->getEgressPrBodyTracker();
  auto res = downstreamCodec_->onIngressDataRejected(42);
  EXPECT_TRUE(res.hasError());
  EXPECT_FALSE(egressPrBodyTracker.bodyStarted());
}

TEST_F(HQPRCodecTest, TestOnIngressBodyRejectedBadstreamOffset) {
  const auto& egressPrBodyTracker = downstreamCodec_->getEgressPrBodyTracker();
  addAndCheckSimpleHeaders();
  EXPECT_FALSE(egressPrBodyTracker.bodyStarted());

  // 1 is before body stream offset, so shouldn't trigger the callback.
  auto res = downstreamCodec_->onIngressDataRejected(1);
  EXPECT_TRUE(res.hasError());
  EXPECT_FALSE(egressPrBodyTracker.bodyStarted());
}

TEST_F(HQPRCodecTest, TestOnIngressBodyRejectedStartWithSkip) {
  const auto& egressPrBodyTracker = downstreamCodec_->getEgressPrBodyTracker();
  HTTPMessage resp = getResponse(200, 500);
  resp.setPartiallyReliable();
  auto streamId = downstreamCodec_->createStream();
  uint64_t bodyStreamOffset = queue_.chainLength();
  downstreamCodec_->generateHeader(queue_, streamId, resp, false, nullptr);
  bodyStreamOffset = queue_.chainLength() - bodyStreamOffset;
  queue_.move();
  EXPECT_TRUE(egressPrBodyTracker.bodyStarted());

  uint64_t testStreamOffset = 73;
  uint64_t expectedBodyAppOffset = testStreamOffset - bodyStreamOffset;

  auto res = downstreamCodec_->onIngressDataRejected(testStreamOffset);
  EXPECT_FALSE(res.hasError());
  EXPECT_EQ(*res, expectedBodyAppOffset);

  auto bodyStreamOffsetStart = egressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, bodyStreamOffset);

  auto data = makeBuf(500);
  downstreamCodec_->generateBody(
      queue_, streamId, std::move(data), folly::none, false);
  queue_.move();

  testStreamOffset = 873;
  expectedBodyAppOffset = testStreamOffset - bodyStreamOffset;
  res = downstreamCodec_->onIngressDataRejected(testStreamOffset);
  EXPECT_FALSE(res.hasError());
  EXPECT_EQ(*res, expectedBodyAppOffset);
  EXPECT_EQ(egressPrBodyTracker.getBodyBytesProcessed(), expectedBodyAppOffset);
}

TEST_F(HQPRCodecTest, TestOnIngressBodyRejectedStartWithBody) {
  const auto& egressPrBodyTracker = downstreamCodec_->getEgressPrBodyTracker();
  HTTPMessage resp = getResponse(200, 500);
  resp.setPartiallyReliable();
  auto streamId = downstreamCodec_->createStream();
  uint64_t bodyStreamOffset = queue_.chainLength();
  downstreamCodec_->generateHeader(queue_, streamId, resp, false, nullptr);
  bodyStreamOffset = queue_.chainLength() - bodyStreamOffset;
  queue_.move();
  EXPECT_TRUE(egressPrBodyTracker.bodyStarted());

  auto data = makeBuf(500);
  uint64_t expectedBodyBytesProcessed = 500;
  downstreamCodec_->generateBody(
      queue_, streamId, std::move(data), folly::none, false);
  queue_.move();

  auto bodyStreamOffsetStart = egressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, bodyStreamOffset);
  EXPECT_EQ(egressPrBodyTracker.getBodyBytesProcessed(),
            expectedBodyBytesProcessed);

  uint64_t testStreamOffset = 836;
  uint64_t expectedBodyAppOffset = testStreamOffset - bodyStreamOffset;
  auto res = downstreamCodec_->onIngressDataRejected(testStreamOffset);
  EXPECT_FALSE(res.hasError());
  EXPECT_EQ(*res, expectedBodyAppOffset);
  EXPECT_EQ(egressPrBodyTracker.getBodyBytesProcessed(), expectedBodyAppOffset);
}

TEST_F(HQPRCodecTest, TestOnEgressBodyExpiredBeforeHeaders) {
  auto res = downstreamCodec_->onEgressBodySkip(13);
  EXPECT_TRUE(res.hasError());
  EXPECT_EQ(res.error(), hq::UnframedBodyOffsetTrackerError::INVALID_OFFSET);
}

TEST_F(HQPRCodecTest, TestOnEgressBodyExpiredStartWithSkip) {
  const auto& egressPrBodyTracker = downstreamCodec_->getEgressPrBodyTracker();
  HTTPMessage resp = getResponse(200, 500);
  resp.setPartiallyReliable();
  auto streamId = downstreamCodec_->createStream();
  uint64_t bodyStreamOffset = queue_.chainLength();
  downstreamCodec_->generateHeader(queue_, streamId, resp, false, nullptr);
  bodyStreamOffset = queue_.chainLength() - bodyStreamOffset;
  queue_.move();
  EXPECT_TRUE(egressPrBodyTracker.bodyStarted());

  uint64_t testAppOffset = 73;

  auto expectedstreamOffset = downstreamCodec_->onEgressBodySkip(testAppOffset);
  auto bodyStreamOffsetStart = egressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, bodyStreamOffset);
  EXPECT_FALSE(expectedstreamOffset.hasError());
  EXPECT_EQ(*expectedstreamOffset, bodyStreamOffset + testAppOffset);

  auto data = makeBuf(500);
  downstreamCodec_->generateBody(
      queue_, streamId, std::move(data), folly::none, false);
  queue_.move();
  EXPECT_EQ(egressPrBodyTracker.getBodyBytesProcessed(), 500 + 73);

  testAppOffset = 817;
  expectedstreamOffset = downstreamCodec_->onEgressBodySkip(testAppOffset);
  EXPECT_FALSE(expectedstreamOffset.hasError());
  EXPECT_EQ(*expectedstreamOffset, bodyStreamOffset + testAppOffset);
}

TEST_F(HQPRCodecTest, TestOnEgressBodyExpiredStartWithBody) {
  const auto& egressPrBodyTracker = downstreamCodec_->getEgressPrBodyTracker();
  HTTPMessage resp = getResponse(200, 500);
  resp.setPartiallyReliable();
  auto streamId = downstreamCodec_->createStream();
  uint64_t bodyStreamOffset = queue_.chainLength();
  downstreamCodec_->generateHeader(queue_, streamId, resp, false, nullptr);
  bodyStreamOffset = queue_.chainLength() - bodyStreamOffset;
  queue_.move();
  EXPECT_TRUE(egressPrBodyTracker.bodyStarted());

  auto data = makeBuf(500);
  downstreamCodec_->generateBody(
      queue_, streamId, std::move(data), folly::none, false);
  queue_.move();
  auto bodyStreamOffsetStart = egressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, bodyStreamOffset);
  EXPECT_EQ(egressPrBodyTracker.getBodyBytesProcessed(), 500);

  uint64_t testAppOffset = 817;
  auto expectedstreamOffset = downstreamCodec_->onEgressBodySkip(testAppOffset);
  EXPECT_FALSE(expectedstreamOffset.hasError());
  EXPECT_EQ(*expectedstreamOffset, bodyStreamOffset + testAppOffset);
}

TEST_F(HQPRCodecTest, TestOnEgressBodyRejectedBeforeHeaders) {
  const auto& ingressPrBodyTracker = upstreamCodec_->getIngressPrBodyTracker();
  auto res = upstreamCodec_->onEgressBodyReject(42);
  EXPECT_TRUE(res.hasError());
  EXPECT_EQ(res.error(), UnframedBodyOffsetTrackerError::INVALID_OFFSET);
  EXPECT_FALSE(ingressPrBodyTracker.bodyStarted());
}

TEST_F(HQPRCodecTest, TestOnEgressBodyRejectedStartWithSkip) {
  const auto& ingressPrBodyTracker = upstreamCodec_->getIngressPrBodyTracker();
  HTTPMessage resp = getResponse(200, 500);
  resp.setPartiallyReliable();
  auto streamId = downstreamCodec_->createStream();
  uint64_t bodyStreamOffset = queue_.chainLength();
  downstreamCodec_->generateHeader(queue_, streamId, resp, false, nullptr);
  bodyStreamOffset = queue_.chainLength() - bodyStreamOffset;
  parseUpstream();
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.headerFrames, 2);
  EXPECT_EQ(callbacks_.bodyCalls, 0);
  EXPECT_TRUE(ingressPrBodyTracker.bodyStarted());

  uint64_t testAppOffset = 73;
  auto streamOffset = upstreamCodec_->onEgressBodyReject(testAppOffset);
  EXPECT_FALSE(streamOffset.hasError());
  EXPECT_EQ(*streamOffset, testAppOffset + bodyStreamOffset);

  auto data = makeBuf(500);
  queue_.append(data->clone());
  parseUpstream();
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.headerFrames, 2);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  auto bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, bodyStreamOffset);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(), testAppOffset + 500);

  testAppOffset = 673;
  streamOffset = upstreamCodec_->onEgressBodyReject(testAppOffset);
  EXPECT_FALSE(streamOffset.hasError());
  EXPECT_EQ(*streamOffset, testAppOffset + bodyStreamOffset);
}

TEST_F(HQPRCodecTest, TestOnEgressBodyRejectedStartWithBody) {
  const auto& ingressPrBodyTracker = upstreamCodec_->getIngressPrBodyTracker();
  HTTPMessage resp = getResponse(200, 500);
  auto streamId = upstreamCodec_->createStream();
  uint64_t bodyStreamOffset = queue_.chainLength();
  downstreamCodec_->generateHeader(queue_, streamId, resp, false, nullptr);
  bodyStreamOffset = queue_.chainLength() - bodyStreamOffset;
  parseUpstream();
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.headerFrames, 1);
  EXPECT_EQ(callbacks_.bodyCalls, 0);
  EXPECT_FALSE(ingressPrBodyTracker.bodyStarted());

  auto data = makeBuf(500);
  bodyStreamOffset +=
      writeFrameHeaderManual(queue_, static_cast<uint8_t>(FrameType::DATA), 0);
  queue_.append(data->clone());
  parseUpstream();
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.headerFrames, 2);
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, data->length());
  auto bodyStreamOffsetStart = ingressPrBodyTracker.getBodyStreamStartOffset();
  EXPECT_FALSE(bodyStreamOffsetStart.hasError());
  EXPECT_EQ(*bodyStreamOffsetStart, bodyStreamOffset);
  EXPECT_EQ(ingressPrBodyTracker.getBodyBytesProcessed(), 500);

  uint64_t testAppOffset = 775;
  auto streamOffset = upstreamCodec_->onEgressBodyReject(testAppOffset);
  EXPECT_FALSE(streamOffset.hasError());
  EXPECT_EQ(*streamOffset, testAppOffset + bodyStreamOffset);
}

TEST_F(HQCodecTest, DataFrameStreaming) {
  auto data1 = makeBuf(500);
  auto data2 = makeBuf(500);
  writeFrameHeaderManual(queue_,
                         static_cast<uint64_t>(FrameType::DATA),
                         data1->length() + data2->length());
  queue_.append(data1->clone());
  parse();
  queue_.append(data2->clone());
  parse();
  EXPECT_EQ(callbacks_.headerFrames, 1);
  EXPECT_EQ(callbacks_.bodyCalls, 2);
  EXPECT_EQ(callbacks_.bodyLength, data1->length() + data2->length());
  auto data3 = makeBuf(100);
  writeFrameHeaderManual(
      queue_, static_cast<uint64_t>(FrameType::DATA), data3->length());
  queue_.append(data3->clone());
  parse();
  EXPECT_EQ(callbacks_.headerFrames, 2);
  EXPECT_EQ(callbacks_.bodyCalls, 3);
  EXPECT_EQ(callbacks_.bodyLength,
            data1->length() + data2->length() + data3->length());
}

TEST_F(HQCodecTest, PushPromiseFrame) {
  hq::PushId pushId = 1234 | kPushIdMask;

  HTTPMessage msg = getGetRequest();
  msg.getHeaders().add(HTTP_HEADER_USER_AGENT, "optimus-prime");

  // push promises can only be sent by the server.
  downstreamCodec_->generatePushPromise(queue_, streamId_, msg, pushId, false);

  // push promises should be parsed by the client
  parseUpstream();

  EXPECT_EQ(callbacks_.headerFrames, 1);
  EXPECT_EQ(callbacks_.bodyCalls, 0);
  EXPECT_EQ(callbacks_.messageBegin, 1);
  EXPECT_EQ(callbacks_.pushId, pushId);
  EXPECT_EQ(callbacks_.assocStreamId, streamId_);
}

template <class T>
void HQCodecTestFixture<T>::qpackTest(bool blocked) {
  QPACKCodec& server = qpackDownstream_;
  server.setDecoderHeaderTableMaxSize(1024);
  qpackUpstream_.setEncoderHeaderTableSize(1024);
  auto streamId = upstreamCodec_->createStream();
  HTTPMessage msg = getGetRequest();
  msg.getHeaders().add(HTTP_HEADER_USER_AGENT, "optimus-prime");
  upstreamCodec_->generateHeader(queue_, streamId, msg, false, nullptr);
  EXPECT_FALSE(qpackUpEncoderWriteBuf_.empty());
  if (!blocked) {
    qpackEncoderCodec_.onUnidirectionalIngress(qpackUpEncoderWriteBuf_.move());
    EXPECT_EQ(callbacks_.sessionErrors, 0);
  }
  TestStreamingCallback cb;
  CHECK(!queue_.empty());
  upstreamCodec_->generateBody(
      queue_, streamId, folly::IOBuf::copyBuffer("ohai"), 0, false);
  upstreamCodec_->generateEOM(queue_, streamId);
  auto consumed = downstreamCodec_->onIngress(*queue_.front());
  EXPECT_TRUE(blocked || consumed == queue_.chainLength());
  queue_.trimStart(consumed);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 0);
  if (blocked) {
    EXPECT_EQ(callbacks_.headersComplete, 0);
    qpackEncoderCodec_.onUnidirectionalIngress(qpackUpEncoderWriteBuf_.move());
    EXPECT_EQ(callbacks_.sessionErrors, 0);
  }
  EXPECT_EQ(callbacks_.headersComplete, 1);
  CHECK(callbacks_.msg);
  if (blocked) {
    EXPECT_FALSE(queue_.empty());
    EXPECT_EQ(callbacks_.bodyCalls, 0);
    EXPECT_EQ(callbacks_.bodyLength, 0);
    consumed = downstreamCodec_->onIngress(*queue_.front());
    queue_.trimStart(consumed);
    EXPECT_TRUE(queue_.empty());
  }
  EXPECT_EQ(callbacks_.bodyCalls, 1);
  EXPECT_EQ(callbacks_.bodyLength, 4);
  downstreamCodec_->onIngressEOF();
  EXPECT_EQ(callbacks_.messageComplete, 1);
  auto ack = server.encodeHeaderAck(streamId);
  if (ack) {
    qpackDecoderCodec_.onUnidirectionalIngress(std::move(ack));
  }
  auto ici = server.encodeInsertCountInc();
  if (ici) {
    qpackDecoderCodec_.onUnidirectionalIngress(std::move(ici));
  }
  EXPECT_EQ(callbacks_.sessionErrors, 0);
}

TEST_F(HQCodecTest, qpack) {
  qpackTest(false);
}

TEST_F(HQCodecTest, qpackBlocked) {
  qpackTest(true);
}

TEST_F(HQCodecTest, qpackError) {
  qpackEncoderCodec_.onUnidirectionalIngress(folly::IOBuf::wrapBuffer("", 1));
  EXPECT_EQ(callbacks_.lastParseError->getErrno(),
            uint32_t(HTTP3::ErrorCode::HTTP_QPACK_ENCODER_STREAM_ERROR));
  EXPECT_EQ(callbacks_.sessionErrors, 1);
  qpackDecoderCodec_.onUnidirectionalIngressEOF();
  EXPECT_EQ(callbacks_.lastParseError->getErrno(),
            uint32_t(HTTP3::ErrorCode::HTTP_CLOSED_CRITICAL_STREAM));
  EXPECT_EQ(callbacks_.sessionErrors, 2);
  qpackEncoderCodec_.onUnidirectionalIngressEOF();
  EXPECT_EQ(callbacks_.lastParseError->getErrno(),
            uint32_t(HTTP3::ErrorCode::HTTP_CLOSED_CRITICAL_STREAM));
  EXPECT_EQ(callbacks_.sessionErrors, 3);

  // duplicate method in headers
  std::vector<compress::Header> headers;
  std::string meth("GET");
  headers.emplace_back(HTTP_HEADER_COLON_METHOD, meth);
  headers.emplace_back(HTTP_HEADER_COLON_METHOD, meth);
  QPACKCodec& client = qpackUpstream_;
  auto result = client.encode(headers, 1);
  hq::writeHeaders(queue_, std::move(result.stream));
  downstreamCodec_->onIngress(*queue_.front());
  EXPECT_EQ(callbacks_.lastParseError->getHttpStatusCode(), 400);
  EXPECT_EQ(callbacks_.streamErrors, 1);
  queue_.move();

  uint8_t bad[] = {0x00}; // LR, no delta base
  hq::writeHeaders(queue_, folly::IOBuf::wrapBuffer(bad, 1));
  downstreamCodec_->onIngress(*queue_.front());
  EXPECT_EQ(callbacks_.lastParseError->getErrno(),
            uint32_t(HTTP3::ErrorCode::HTTP_QPACK_DECOMPRESSION_FAILED));
  EXPECT_EQ(callbacks_.sessionErrors, 4);
}

TEST_F(HQCodecTest, extraHeaders) {
  std::array<uint8_t, 6> simpleReq{0x00, 0x00, 0xC0, 0xC1, 0xD1, 0xD7};
  std::array<uint8_t, 3> simpleResp{0x00, 0x00, 0xD9};
  writeFrameHeaderManual(
      queue_, static_cast<uint64_t>(FrameType::HEADERS), simpleReq.size());
  queue_.append(simpleReq.data(), simpleReq.size());
  downstreamCodec_->onIngress(*queue_.front());
  EXPECT_EQ(callbacks_.streamErrors, 0);
  downstreamCodec_->onIngress(*queue_.front());
  EXPECT_EQ(callbacks_.streamErrors, 1);
  queue_.move();
  writeFrameHeaderManual(
      queue_, static_cast<uint64_t>(FrameType::HEADERS), simpleResp.size());
  queue_.append(simpleResp.data(), simpleResp.size());
  upstreamCodec_->onIngress(*queue_.front());
  EXPECT_EQ(callbacks_.streamErrors, 1);
  upstreamCodec_->onIngress(*queue_.front());
  EXPECT_EQ(callbacks_.streamErrors, 2);
}

/* test that parsing multiple frame headers stops the parser */
TEST_F(HQCodecTest, MultipleHeaders) {
  writeValidFrame(queue_, FrameType::HEADERS);

  writeFrameHeaderManual(
      queue_, static_cast<uint64_t>(FrameType::HEADERS), 0x08);

  std::array<uint8_t, 10> data{
      0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  queue_.append(data.data(), data.size());
  parse();
}

TEST_F(HQCodecTest, MultipleSettingsUpstream) {
  writeValidFrame(queueCtrl_, FrameType::SETTINGS);
  writeValidFrame(queueCtrl_, FrameType::SETTINGS);
  parseControl(CodecType::CONTROL_UPSTREAM);
  EXPECT_EQ(callbacks_.headerFrames, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
}

TEST_F(HQCodecTest, MultipleSettingsDownstream) {
  writeValidFrame(queueCtrl_, FrameType::SETTINGS);
  writeValidFrame(queueCtrl_, FrameType::SETTINGS);
  parseControl(CodecType::CONTROL_DOWNSTREAM);
  EXPECT_EQ(callbacks_.headerFrames, 1);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, 1);
}

struct FrameAllowedParams {
  CodecType codecType;
  FrameType frameType;
  bool allowed;
};

std::string frameParamsToTestName(
    const testing::TestParamInfo<FrameAllowedParams>& info) {
  std::string testName = "";
  switch (info.param.codecType) {
    case CodecType::CONTROL_UPSTREAM:
    case CodecType::H1Q_CONTROL_UPSTREAM:
      testName = "UpstreamControl";
      break;
    case CodecType::CONTROL_DOWNSTREAM:
    case CodecType::H1Q_CONTROL_DOWNSTREAM:
      testName = "DownstreamControl";
      break;
    case CodecType::UPSTREAM:
      testName = "Upstream";
      break;
    case CodecType::DOWNSTREAM:
      testName = "Downstream";
      break;
    default:
      LOG(FATAL) << "Unknown Codec Type";
      break;
  }
  switch (info.param.frameType) {
    case FrameType::DATA:
      testName += "Data";
      break;
    case FrameType::HEADERS:
      testName += "Headers";
      break;
    case FrameType::CANCEL_PUSH:
      testName += "CancelPush";
      break;
    case FrameType::SETTINGS:
      testName += "Settings";
      break;
    case FrameType::PUSH_PROMISE:
      testName += "PushPromise";
      break;
    case FrameType::GOAWAY:
      testName += "Goaway";
      break;
    case FrameType::MAX_PUSH_ID:
      testName += "MaxPushID";
      break;
    default:
      testName +=
          folly::to<std::string>(static_cast<uint64_t>(info.param.frameType));
      break;
  }
  return testName;
}

class HQCodecTestFrameAllowed
    : public HQCodecTestFixture<TestWithParam<FrameAllowedParams>> {};

TEST_P(HQCodecTestFrameAllowed, FrameAllowedOnCodec) {
  int expectedFrames = 0;
  switch (GetParam().codecType) {
    case CodecType::CONTROL_UPSTREAM:
    case CodecType::CONTROL_DOWNSTREAM:
      // SETTINGS MUST be the first frame on the CONTROL stream
      if (GetParam().frameType != FrameType::SETTINGS) {
        writeValidFrame(queueCtrl_, FrameType::SETTINGS);
        expectedFrames++;
      }
      writeValidFrame(queueCtrl_, GetParam().frameType);
      // add some extra trailing junk to the input buffer
      queueCtrl_.append(IOBuf::copyBuffer("j"));
      parseControl(GetParam().codecType);
      break;
    case CodecType::H1Q_CONTROL_UPSTREAM:
      writeValidFrame(queueCtrl_, GetParam().frameType);
      queueCtrl_.append(IOBuf::copyBuffer("j"));
      parseControl(GetParam().codecType);
      break;
    case CodecType::UPSTREAM:
      writeValidFrame(queue_, GetParam().frameType);
      queue_.append(IOBuf::copyBuffer("j"));
      parseUpstream();
      break;
    case CodecType::DOWNSTREAM:
      writeValidFrame(queue_, GetParam().frameType);
      queue_.append(IOBuf::copyBuffer("j"));
      parse();
      break;
    default:
      CHECK(false);
      break;
  }
  expectedFrames += GetParam().allowed ? 1 : 0;
  EXPECT_EQ(callbacks_.headerFrames, expectedFrames);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, GetParam().allowed ? 0 : 1);
  // If an error was triggered, check that any additional parse call does not
  // raise another error, and that no new bytes are parsed
  if (!GetParam().allowed) {
    auto lenBefore = 0;
    auto lenAfter = 0;
    switch (GetParam().codecType) {
      case CodecType::CONTROL_UPSTREAM:
      case CodecType::CONTROL_DOWNSTREAM:
      case CodecType::H1Q_CONTROL_UPSTREAM:
        lenBefore = queueCtrl_.chainLength();
        parseControl(GetParam().codecType);
        lenAfter = queueCtrl_.chainLength();
        break;
      case CodecType::UPSTREAM:
        lenBefore = queue_.chainLength();
        parseUpstream();
        lenAfter = queue_.chainLength();
        break;
      case CodecType::DOWNSTREAM:
        lenBefore = queue_.chainLength();
        parse();
        lenAfter = queue_.chainLength();
        break;
      default:
        CHECK(false);
        break;
    }
    EXPECT_EQ(lenBefore, lenAfter);
    EXPECT_EQ(callbacks_.headerFrames, expectedFrames);
    EXPECT_EQ(callbacks_.streamErrors, 0);
    EXPECT_EQ(callbacks_.sessionErrors, 1);
  }
}

INSTANTIATE_TEST_CASE_P(
    FrameAllowedTests,
    HQCodecTestFrameAllowed,
    Values(
        (FrameAllowedParams){CodecType::DOWNSTREAM, FrameType::DATA, true},
        (FrameAllowedParams){CodecType::DOWNSTREAM, FrameType::HEADERS, true},
        (FrameAllowedParams){
            CodecType::DOWNSTREAM, FrameType::CANCEL_PUSH, false},
        (FrameAllowedParams){CodecType::DOWNSTREAM, FrameType::SETTINGS, false},
        (FrameAllowedParams){
            CodecType::DOWNSTREAM, FrameType::PUSH_PROMISE, false},
        (FrameAllowedParams){
            CodecType::UPSTREAM, FrameType::PUSH_PROMISE, true},
        (FrameAllowedParams){CodecType::DOWNSTREAM, FrameType::GOAWAY, false},
        (FrameAllowedParams){
            CodecType::DOWNSTREAM, FrameType::MAX_PUSH_ID, false},
        (FrameAllowedParams){
            CodecType::DOWNSTREAM, FrameType(*getGreaseId(0)), true},
        (FrameAllowedParams){CodecType::DOWNSTREAM,
                             FrameType(*getGreaseId(hq::kMaxGreaseIdIndex)),
                             true},
        // HQ Upstream Ingress Control Codec
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::DATA, false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::HEADERS, false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::CANCEL_PUSH, true},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::SETTINGS, true},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::PUSH_PROMISE, false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::GOAWAY, true},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::MAX_PUSH_ID, false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType(*getGreaseId(12345)), true},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType(*getGreaseId(54321)), true},
        // HQ Downstream Ingress Control Codec
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::DATA, false},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::HEADERS, false},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::CANCEL_PUSH, true},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::SETTINGS, true},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::PUSH_PROMISE, false},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::GOAWAY, false},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::MAX_PUSH_ID, true},
        (FrameAllowedParams){CodecType::CONTROL_DOWNSTREAM,
                             FrameType(*getGreaseId(98765)),
                             true},
        (FrameAllowedParams){CodecType::CONTROL_DOWNSTREAM,
                             FrameType(*getGreaseId(567879)),
                             true}),
    frameParamsToTestName);

class H1QCodecTestFrameAllowed
    : public HQCodecTestFixture<TestWithParam<FrameAllowedParams>> {};

TEST_P(H1QCodecTestFrameAllowed, FrameAllowedOnH1qControlCodec) {
  writeValidFrame(queueCtrl_, GetParam().frameType);
  parseControl(GetParam().codecType);
  EXPECT_EQ(callbacks_.headerFrames, GetParam().allowed ? 1 : 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, GetParam().allowed ? 0 : 1);
}

INSTANTIATE_TEST_CASE_P(
    H1QFrameAllowedTests,
    H1QCodecTestFrameAllowed,
    Values(
        // H1Q Upstream Ingress Control Codec
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_UPSTREAM, FrameType::DATA, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_UPSTREAM, FrameType::HEADERS, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_UPSTREAM, FrameType::CANCEL_PUSH, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_UPSTREAM, FrameType::SETTINGS, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_UPSTREAM, FrameType::PUSH_PROMISE, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_UPSTREAM, FrameType::GOAWAY, true},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_UPSTREAM, FrameType::MAX_PUSH_ID, false},
        (FrameAllowedParams){CodecType::H1Q_CONTROL_UPSTREAM,
                             FrameType(*getGreaseId(123456789)),
                             false},
        (FrameAllowedParams){CodecType::H1Q_CONTROL_UPSTREAM,
                             FrameType(*getGreaseId(987654321)),
                             false},
        // H1Q Downstream Ingress Control Codec
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_DOWNSTREAM, FrameType::DATA, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_DOWNSTREAM, FrameType::HEADERS, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_DOWNSTREAM, FrameType::CANCEL_PUSH, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_DOWNSTREAM, FrameType::SETTINGS, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_DOWNSTREAM, FrameType::PUSH_PROMISE, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_DOWNSTREAM, FrameType::GOAWAY, false},
        (FrameAllowedParams){
            CodecType::H1Q_CONTROL_DOWNSTREAM, FrameType::MAX_PUSH_ID, false},
        (FrameAllowedParams){CodecType::H1Q_CONTROL_DOWNSTREAM,
                             FrameType(*getGreaseId(13579)),
                             false},
        (FrameAllowedParams){CodecType::H1Q_CONTROL_DOWNSTREAM,
                             FrameType(*getGreaseId(97531)),
                             false}),
    frameParamsToTestName);

class HQCodecTestFrameBeforeSettings
    : public HQCodecTestFixture<TestWithParam<FrameAllowedParams>> {};

TEST_P(HQCodecTestFrameBeforeSettings, FrameAllowedOnH1qControlCodec) {
  writeValidFrame(queueCtrl_, GetParam().frameType);
  parseControl(GetParam().codecType);
  EXPECT_EQ(callbacks_.headerFrames, GetParam().allowed ? 1 : 0);
  EXPECT_EQ(callbacks_.streamErrors, 0);
  EXPECT_EQ(callbacks_.sessionErrors, GetParam().allowed ? 0 : 1);
}

INSTANTIATE_TEST_CASE_P(
    FrameBeforeSettingsTests,
    HQCodecTestFrameBeforeSettings,
    Values(
        // HQ Upstream Ingress Control Codec
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::DATA, false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::HEADERS, false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::CANCEL_PUSH, false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::PUSH_PROMISE, false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::GOAWAY, false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType::MAX_PUSH_ID, false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType(*getGreaseId(24680)), false},
        (FrameAllowedParams){
            CodecType::CONTROL_UPSTREAM, FrameType(*getGreaseId(8642)), false},
        // HQ Downstream Ingress Control Codec
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::DATA, false},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::HEADERS, false},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::CANCEL_PUSH, false},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::PUSH_PROMISE, false},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::GOAWAY, false},
        (FrameAllowedParams){
            CodecType::CONTROL_DOWNSTREAM, FrameType::MAX_PUSH_ID, false},
        (FrameAllowedParams){CodecType::CONTROL_DOWNSTREAM,
                             FrameType(*getGreaseId(12121212)),
                             false},
        (FrameAllowedParams){CodecType::CONTROL_DOWNSTREAM,
                             FrameType(*getGreaseId(3434343434)),
                             false}),
    frameParamsToTestName);
