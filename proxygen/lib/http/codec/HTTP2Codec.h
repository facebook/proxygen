/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/HeaderDecodeInfo.h>
#include <proxygen/lib/http/codec/HTTPRequestVerifier.h>
#include <proxygen/lib/http/codec/HTTP2Framer.h>
#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/http/codec/HTTPParallelCodec.h>
#include <proxygen/lib/http/codec/HTTPSettings.h>
#include <proxygen/lib/http/codec/compress/HPACKCodec.h>

#include <bitset>
#include <set>

namespace proxygen {

/**
 * An implementation of the framing layer for HTTP/2. Instances of this
 * class must not be used from multiple threads concurrently.
 */
class HTTP2Codec: public HTTPParallelCodec, HPACK::StreamingCallback {
public:
  void onHeader(const folly::fbstring& name,
                const folly::fbstring& value) override;
  void onHeadersComplete(HTTPHeaderSize decodedSize) override;
  void onDecodeError(HPACK::DecodeError decodeError) override;

  explicit HTTP2Codec(TransportDirection direction);
  ~HTTP2Codec() override;

  // HTTPCodec API
  CodecProtocol getProtocol() const override {
    return CodecProtocol::HTTP_2;
  }

  const std::string& getUserAgent() const override {
    return userAgent_;
  }

  size_t onIngress(const folly::IOBuf& buf) override;
  bool onIngressUpgradeMessage(const HTTPMessage& msg) override;
  size_t generateConnectionPreface(folly::IOBufQueue& writeBuf) override;
  void generateHeader(folly::IOBufQueue& writeBuf,
                      StreamID stream,
                      const HTTPMessage& msg,
                      bool eom = false,
                      HTTPHeaderSize* size = nullptr) override;
  void generateContinuation(folly::IOBufQueue& writeBuf,
                            folly::IOBufQueue& queue,
                            StreamID stream,
                            size_t maxFrameSize);
  void generatePushPromise(folly::IOBufQueue& writeBuf,
                           StreamID stream,
                           const HTTPMessage& msg,
                           StreamID assocStream,
                           bool eom = false,
                           HTTPHeaderSize* size = nullptr) override;
  void generateExHeader(folly::IOBufQueue& writeBuf,
                        StreamID stream,
                        const HTTPMessage& msg,
                        const HTTPCodec::ExAttributes& exAttributes,
                        bool eom = false,
                        HTTPHeaderSize* size = nullptr) override;
  size_t generateBody(folly::IOBufQueue& writeBuf,
                      StreamID stream,
                      std::unique_ptr<folly::IOBuf> chain,
                      folly::Optional<uint8_t> padding,
                      bool eom) override;
  size_t generateChunkHeader(folly::IOBufQueue& writeBuf,
                             StreamID stream,
                             size_t length) override;
  size_t generateChunkTerminator(folly::IOBufQueue& writeBuf,
                                 StreamID stream) override;
  size_t generateTrailers(folly::IOBufQueue& writeBuf,
                          StreamID stream,
                          const HTTPHeaders& trailers) override;
  size_t generateEOM(folly::IOBufQueue& writeBuf,
                     StreamID stream) override;
  size_t generateRstStream(folly::IOBufQueue& writeBuf,
                           StreamID stream,
                           ErrorCode statusCode) override;
  size_t generateGoaway(
    folly::IOBufQueue& writeBuf,
    StreamID lastStream,
    ErrorCode statusCode,
    std::unique_ptr<folly::IOBuf> debugData = nullptr) override;
  size_t generatePingRequest(folly::IOBufQueue& writeBuf) override;
  size_t generatePingReply(folly::IOBufQueue& writeBuf,
                           uint64_t uniqueID) override;
  size_t generateSettings(folly::IOBufQueue& writeBuf) override;
  size_t generateSettingsAck(folly::IOBufQueue& writeBuf) override;
  size_t generateWindowUpdate(folly::IOBufQueue& writeBuf,
                              StreamID stream,
                              uint32_t delta) override;
  size_t generatePriority(folly::IOBufQueue& writeBuf,
                          StreamID stream,
                          const HTTPMessage::HTTPPriority& pri) override;
  size_t generateCertificateRequest(
      folly::IOBufQueue& writeBuf,
      uint16_t requestId,
      std::unique_ptr<folly::IOBuf> certificateRequestData) override;
  size_t generateCertificate(folly::IOBufQueue& writeBuf,
                             uint16_t certId,
                             std::unique_ptr<folly::IOBuf> certData) override;
  const HTTPSettings* getIngressSettings() const override {
    return &ingressSettings_;
  }
  HTTPSettings* getEgressSettings() override { return &egressSettings_; }
  uint32_t getDefaultWindowSize() const override {
    return http2::kInitialWindow;
  }
  bool supportsPushTransactions() const override {
    return
      (transportDirection_ == TransportDirection::DOWNSTREAM &&
       ingressSettings_.getSetting(SettingsId::ENABLE_PUSH, 1)) ||
      (transportDirection_ == TransportDirection::UPSTREAM &&
       egressSettings_.getSetting(SettingsId::ENABLE_PUSH, 1));
  }
  bool peerHasWebsockets() const {
    return ingressSettings_.getSetting(SettingsId::ENABLE_CONNECT_PROTOCOL);
  }
  bool supportsExTransactions() const override {
    return ingressSettings_.getSetting(SettingsId::ENABLE_EX_HEADERS, 0) &&
      egressSettings_.getSetting(SettingsId::ENABLE_EX_HEADERS, 0);
  }
  void setHeaderCodecStats(HeaderCodec::Stats* hcStats) override {
    headerCodec_.setStats(hcStats);
  }

  bool isRequest(StreamID id) const {
    return ((transportDirection_ == TransportDirection::DOWNSTREAM &&
             (id & 0x1) == 1) ||
            (transportDirection_ == TransportDirection::UPSTREAM &&
             (id & 0x1) == 0));
  }

  size_t addPriorityNodes(
      PriorityQueue& queue,
      folly::IOBufQueue& writeBuf,
      uint8_t maxLevel) override;
  HTTPCodec::StreamID mapPriorityToDependency(uint8_t priority) const override;

  HPACKTableInfo getHPACKTableInfo() const override {
    return headerCodec_.getHPACKTableInfo();
  }

  //HTTP2Codec specific API

  static void requestUpgrade(HTTPMessage& request);

#ifndef NDEBUG
  uint64_t getReceivedFrameCount() const {
    return receivedFrameCount_;
  }
#endif

  // Whether turn on the optimization to reuse IOBuf headroom when write DATA
  // frame. For other frames, it's always ON.
  void setReuseIOBufHeadroomForData(bool enabled) {
    reuseIOBufHeadroomForData_ = enabled;
  }

  void setHeaderIndexingStrategy(const HeaderIndexingStrategy* indexingStrat) {
    headerCodec_.setHeaderIndexingStrategy(indexingStrat);
  }
  const HeaderIndexingStrategy* getHeaderIndexingStrategy() const {
    return headerCodec_.getHeaderIndexingStrategy();
  }

 private:
  void generateHeaderImpl(folly::IOBufQueue& writeBuf,
                          StreamID stream,
                          const HTTPMessage& msg,
                          folly::Optional<StreamID> assocStream,
                          folly::Optional<ExAttributes> exAttributes,
                          bool eom,
                          HTTPHeaderSize* size);
  std::unique_ptr<folly::IOBuf> encodeHeaders(
      const HTTPHeaders& headers,
      std::vector<compress::Header>& allHeaders,
      HTTPHeaderSize* size);

  size_t generateHeaderCallbackWrapper(StreamID stream, http2::FrameType type, size_t length);

  ErrorCode parseFrame(folly::io::Cursor& cursor);
  ErrorCode parseAllData(folly::io::Cursor& cursor);
  ErrorCode parseDataFrameData(
    folly::io::Cursor& cursor,
    size_t bufLen,
    size_t& parsed);
  ErrorCode parseHeaders(folly::io::Cursor& cursor);
  ErrorCode parseExHeaders(folly::io::Cursor& cursor);
  ErrorCode parsePriority(folly::io::Cursor& cursor);
  ErrorCode parseRstStream(folly::io::Cursor& cursor);
  ErrorCode parseSettings(folly::io::Cursor& cursor);
  ErrorCode parsePushPromise(folly::io::Cursor& cursor);
  ErrorCode parsePing(folly::io::Cursor& cursor);
  ErrorCode parseGoaway(folly::io::Cursor& cursor);
  ErrorCode parseContinuation(folly::io::Cursor& cursor);
  ErrorCode parseWindowUpdate(folly::io::Cursor& cursor);
  ErrorCode parseCertificateRequest(folly::io::Cursor& cursor);
  ErrorCode parseCertificate(folly::io::Cursor& cursor);
  ErrorCode parseHeadersImpl(
    folly::io::Cursor& cursor,
    std::unique_ptr<folly::IOBuf> headerBuf,
    folly::Optional<http2::PriorityUpdate> priority,
    folly::Optional<uint32_t> promisedStream,
    folly::Optional<ExAttributes> exAttributes);
  folly::Optional<ErrorCode> parseHeadersDecodeFrames(
      folly::Optional<http2::PriorityUpdate> priority,
      folly::Optional<uint32_t> promisedStream,
      folly::Optional<ExAttributes> exAttributes,
      std::unique_ptr<HTTPMessage>& msg);
  folly::Optional<ErrorCode> parseHeadersCheckConcurrentStreams(
      folly::Optional<http2::PriorityUpdate> priority);

  ErrorCode handleEndStream();
  ErrorCode checkNewStream(uint32_t stream, bool trailersAllowed);
  bool checkConnectionError(ErrorCode, const folly::IOBuf* buf);
  ErrorCode handleSettings(const std::deque<SettingPair>& settings);
  size_t maxSendFrameSize() const {
    return (uint32_t)ingressSettings_.getSetting(SettingsId::MAX_FRAME_SIZE,
                                       http2::kMaxFramePayloadLengthMin);
  }
  uint32_t maxRecvFrameSize() const {
    return (uint32_t)egressSettings_.getSetting(SettingsId::MAX_FRAME_SIZE,
                                      http2::kMaxFramePayloadLengthMin);
  }
  void streamError(const std::string& msg, ErrorCode error, bool newTxn=false);
  bool parsingTrailers() const;

  HPACKCodec headerCodec_;

  // Current frame state
  http2::FrameHeader curHeader_;
  StreamID expectedContinuationStream_{0};
  bool pendingEndStreamHandling_{false};
  bool ingressWebsocketUpgrade_{false};

  std::unordered_set<StreamID> upgradedStreams_;

  uint16_t curCertId_{0};
  folly::IOBufQueue curAuthenticatorBlock_{
      folly::IOBufQueue::cacheChainLength()};

  folly::IOBufQueue curHeaderBlock_{folly::IOBufQueue::cacheChainLength()};
  HTTPSettings ingressSettings_{
    { SettingsId::HEADER_TABLE_SIZE, 4096 },
    { SettingsId::ENABLE_PUSH, 1 },
    { SettingsId::MAX_FRAME_SIZE, 16384 },
  };
  HTTPSettings egressSettings_{
    { SettingsId::HEADER_TABLE_SIZE, 4096 },
    { SettingsId::ENABLE_PUSH, 0 },
    { SettingsId::MAX_FRAME_SIZE, 16384 },
    { SettingsId::MAX_HEADER_LIST_SIZE, 1 << 17 }, // same as SPDYCodec
  };
#ifndef NDEBUG
  uint64_t receivedFrameCount_{0};
#endif
  enum FrameState {
    UPSTREAM_CONNECTION_PREFACE = 0,
    DOWNSTREAM_CONNECTION_PREFACE = 1,
    FRAME_HEADER = 2,
    FRAME_DATA = 3,
    DATA_FRAME_DATA = 4,
  };
  FrameState frameState_:3;
  std::string userAgent_;

  size_t pendingDataFrameBytes_{0};
  size_t pendingDataFramePaddingBytes_{0};

  HeaderDecodeInfo decodeInfo_;
  std::vector<StreamID> virtualPriorityNodes_;
  bool reuseIOBufHeadroomForData_{true};

  // True if last parsed HEADERS frame was trailers.
  // Reset only when HEADERS frame is parsed, thus
  // remains unchanged and used during CONTINUATION frame
  // parsing as well.
  // Applies only to DOWNSTREAM, for UPSTREAM we use
  // diffrent heuristic - lack of status code.
  bool parsingDownstreamTrailers_{false};

  // CONTINUATION frame can follow either HEADERS or PUSH_PROMISE frames.
  // Keeps frame type of iniating frame of header block.
  http2::FrameType headerBlockFrameType_{http2::FrameType::DATA};
};

} // proxygen
