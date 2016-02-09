/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/experimental/HTTPRequestVerifier.h>
#include <proxygen/lib/http/codec/experimental/HTTP2Framer.h>
#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/http/codec/HTTPParallelCodec.h>
#include <proxygen/lib/http/codec/HTTPSettings.h>
#include <proxygen/lib/utils/Result.h>

#include <proxygen/lib/http/codec/compress/experimental/hpack9/HPACKCodec.h>

#include <bitset>

namespace proxygen {

/**
 * An implementation of the framing layer for HTTP/2. Instances of this
 * class must not be used from multiple threads concurrently.
 */
class HTTP2Codec: public HTTPParallelCodec, HeaderCodec::StreamingCallback {
public:
  void onHeader(const std::string& name,
                const std::string& value) override;
  void onHeadersComplete() override;
  void onDecodeError(HeaderDecodeError decodeError) override;

  explicit HTTP2Codec(TransportDirection direction);
  ~HTTP2Codec() override;

  // HTTPCodec API
  CodecProtocol getProtocol() const override {
    return CodecProtocol::HTTP_2;
  }

  size_t onIngress(const folly::IOBuf& buf) override;
  size_t generateConnectionPreface(folly::IOBufQueue& writeBuf) override;
  void generateHeader(folly::IOBufQueue& writeBuf,
                      StreamID stream,
                      const HTTPMessage& msg,
                      StreamID assocStream = 0,
                      bool eom = false,
                      HTTPHeaderSize* size = nullptr) override;
  size_t generateBody(folly::IOBufQueue& writeBuf,
                      StreamID stream,
                      std::unique_ptr<folly::IOBuf> chain,
                      boost::optional<uint8_t> padding,
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
  size_t generateGoaway(folly::IOBufQueue& writeBuf,
                        StreamID lastStream,
                        ErrorCode statusCode) override;
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

  //HTTP2Codec specific API

#ifndef NDEBUG
  uint64_t getReceivedFrameCount() const {
    return receivedFrameCount_;
  }
#endif

  static void setHeaderSplitSize(uint32_t splitSize) {
    kHeaderSplitSize = splitSize;
  }

 private:
  friend struct HTTP2CodecInitPerHopHeaders;

  class HeaderDecodeInfo {
   public:
    explicit HeaderDecodeInfo(HTTPRequestVerifier v)
    : verifier(v) {}

    void init(HTTPMessage* msgIn, bool isRequestIn) {
      msg = msgIn;
      isRequest = isRequestIn;
      hasStatus = false;
      regularHeaderSeen = false;
      parsingError = "";
      decodeError = HeaderDecodeError::NONE;
      verifier.error = "";
      verifier.setMessage(msg);
      verifier.setHasMethod(false);
      verifier.setHasPath(false);
      verifier.setHasScheme(false);
      verifier.setHasAuthority(false);
    }
    // Change this to a map of decoded header blocks when we decide
    // to concurrently decode partial header blocks
    HTTPMessage* msg{nullptr};
    HTTPRequestVerifier verifier;
    bool isRequest{false};
    bool hasStatus{false};
    bool regularHeaderSeen{false};
    std::string parsingError;
    HeaderDecodeError decodeError{HeaderDecodeError::NONE};
  };

  /**
   * Determines whether header with a given code is on the SPDY per-hop
   * header blacklist.
   */
  static std::bitset<256> perHopHeaderCodes_;

  ErrorCode parseFrame(folly::io::Cursor& cursor);
  ErrorCode parseAllData(folly::io::Cursor& cursor);
  ErrorCode parseDataFrameData(
    folly::io::Cursor& cursor,
    size_t bufLen,
    size_t& parsed);
  ErrorCode parseHeaders(folly::io::Cursor& cursor);
  ErrorCode parsePriority(folly::io::Cursor& cursor);
  ErrorCode parseRstStream(folly::io::Cursor& cursor);
  ErrorCode parseSettings(folly::io::Cursor& cursor);
  ErrorCode parsePushPromise(folly::io::Cursor& cursor);
  ErrorCode parsePing(folly::io::Cursor& cursor);
  ErrorCode parseGoaway(folly::io::Cursor& cursor);
  ErrorCode parseContinuation(folly::io::Cursor& cursor);
  ErrorCode parseWindowUpdate(folly::io::Cursor& cursor);
  ErrorCode parseHeadersImpl(
    folly::io::Cursor& cursor,
    std::unique_ptr<folly::IOBuf> headerBuf,
    boost::optional<http2::PriorityUpdate> priority,
    boost::optional<uint32_t> promisedStream);

  ErrorCode handleEndStream();
  ErrorCode checkNewStream(uint32_t stream);
  bool checkConnectionError(ErrorCode, const folly::IOBuf* buf);
  uint32_t maxSendFrameSize() const {
    return ingressSettings_.getSetting(SettingsId::MAX_FRAME_SIZE,
                                       http2::kMaxFramePayloadLengthMin);
  }
  uint32_t maxRecvFrameSize() const {
    return egressSettings_.getSetting(SettingsId::MAX_FRAME_SIZE,
                                      http2::kMaxFramePayloadLengthMin);
  }
  void streamError(const std::string& msg, ErrorCode error, bool newTxn=false);

  HPACKCodec09 headerCodec_;

  // Current frame state
  http2::FrameHeader curHeader_;
  StreamID expectedContinuationStream_{0};
  bool pendingEndStreamHandling_{false};
  bool needsChromeWorkaround_{false}; // malformed continuation
  bool needsChromeWorkaround2_{false}; // rst on 16kb
  std::set<HTTPCodec::StreamID> expectedChromeResets_;

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
  uint32_t egressGoawayAck_{std::numeric_limits<uint32_t>::max()};
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

  static uint32_t kHeaderSplitSize;
  HeaderDecodeInfo decodeInfo_;
};

} // proxygen
