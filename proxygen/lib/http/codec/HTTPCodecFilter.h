/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/utils/FilterChain.h>

namespace proxygen {

using HTTPCodecFilter = GenericFilter<HTTPCodec,
                                      HTTPCodec::Callback,
                                      &HTTPCodec::setCallback,
                                      true>;

/**
 * An implementation of HTTPCodecFilter that passes through all calls. This is
 * useful to subclass if you aren't interested in intercepting every function.
 * See HTTPCodec.h for documentation on these methods.
 */
class PassThroughHTTPCodecFilter : public HTTPCodecFilter {
 public:
  /**
   * By default, the filter gets both calls and callbacks
   */
  explicit PassThroughHTTPCodecFilter(bool calls = true, bool callbacks = true)
      : HTTPCodecFilter(calls, callbacks) {
  }

  // HTTPCodec::Callback methods
  void onMessageBegin(StreamID stream, HTTPMessage* msg) override;

  void onPushMessageBegin(StreamID stream,
                          StreamID assocStream,
                          HTTPMessage* msg) override;

  void onHeadersComplete(StreamID stream,
                         std::unique_ptr<HTTPMessage> msg) override;

  void onBody(StreamID stream,
              std::unique_ptr<folly::IOBuf> chain,
              uint16_t padding) override;

  void onChunkHeader(StreamID stream, size_t length) override;

  void onChunkComplete(StreamID stream) override;

  void onTrailersComplete(StreamID stream,
                          std::unique_ptr<HTTPHeaders> trailers) override;

  void onMessageComplete(StreamID stream, bool upgrade) override;

  void onFrameHeader(StreamID stream_id,
                     uint8_t flags,
                     uint64_t length,
                     uint64_t type,
                     uint16_t version = 0) override;

  void onError(StreamID stream,
               const HTTPException& error,
               bool newStream = false) override;

  void onAbort(StreamID stream, ErrorCode code) override;

  void onGoaway(uint64_t lastGoodStreamID,
                ErrorCode code,
                std::unique_ptr<folly::IOBuf> debugData = nullptr) override;

  void onPingRequest(uint64_t data) override;

  void onPingReply(uint64_t data) override;

  void onWindowUpdate(StreamID stream, uint32_t amount) override;

  void onSettings(const SettingsList& settings) override;

  void onSettingsAck() override;

  void onPriority(StreamID stream, const HTTPPriority& pri) override;

  void onPushPriority(StreamID stream, const HTTPPriority& pri) override;

  void onGenerateFrameHeader(StreamID streamID,
                             uint8_t type,
                             uint64_t length,
                             uint16_t version) override;

  [[nodiscard]] uint32_t numOutgoingStreams() const override;

  [[nodiscard]] uint32_t numIncomingStreams() const override;

  // HTTPCodec methods
  [[nodiscard]] CompressionInfo getCompressionInfo() const override;

  [[nodiscard]] CodecProtocol getProtocol() const override;

  [[nodiscard]] const std::string& getUserAgent() const override;

  [[nodiscard]] TransportDirection getTransportDirection() const override;

  [[nodiscard]] bool supportsStreamFlowControl() const override;

  [[nodiscard]] bool supportsSessionFlowControl() const override;

  StreamID createStream() override;

  void setCallback(HTTPCodec::Callback* callback) override;

  [[nodiscard]] bool isBusy() const override;

  void setParserPaused(bool paused) override;

  [[nodiscard]] bool isParserPaused() const override;

  size_t onIngress(const folly::IOBuf& buf) override;

  void onIngressEOF() override;

  [[nodiscard]] bool isReusable() const override;

  [[nodiscard]] bool isWaitingToDrain() const override;

  [[nodiscard]] bool closeOnEgressComplete() const override;

  [[nodiscard]] bool supportsParallelRequests() const override;

  [[nodiscard]] bool supportsPushTransactions() const override;

  size_t generateConnectionPreface(folly::IOBufQueue& writeBuf) override;

  void generateHeader(
      folly::IOBufQueue& writeBuf,
      StreamID stream,
      const HTTPMessage& msg,
      bool eom,
      HTTPHeaderSize* size,
      const folly::Optional<HTTPHeaders>& extraHeaders) override;

  void generatePushPromise(folly::IOBufQueue& writeBuf,
                           StreamID stream,
                           const HTTPMessage& msg,
                           StreamID assocStream,
                           bool eom,
                           HTTPHeaderSize* size) override;

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

  size_t generatePadding(folly::IOBufQueue& writeBuf,
                         StreamID stream,
                         uint16_t bytes) override;

  size_t generateEOM(folly::IOBufQueue& writeBuf, StreamID stream) override;

  size_t generateRstStream(folly::IOBufQueue& writeBuf,
                           StreamID stream,
                           ErrorCode statusCode) override;

  size_t generateGoaway(
      folly::IOBufQueue& writeBuf,
      StreamID lastStream,
      ErrorCode statusCode,
      std::unique_ptr<folly::IOBuf> debugData = nullptr) override;

  size_t generatePingRequest(
      folly::IOBufQueue& writeBuf,
      folly::Optional<uint64_t> data = folly::none) override;

  size_t generatePingReply(folly::IOBufQueue& writeBuf, uint64_t data) override;

  size_t generateSettings(folly::IOBufQueue& writeBuf) override;

  size_t generateSettingsAck(folly::IOBufQueue& writeBuf) override;

  size_t generateWindowUpdate(folly::IOBufQueue& writeBuf,
                              StreamID stream,
                              uint32_t delta) override;

  size_t generatePriority(folly::IOBufQueue& writeBuf,
                          StreamID streamId,
                          HTTPPriority priority) override;

  size_t generatePushPriority(folly::IOBufQueue& writeBuf,
                              StreamID pushId,
                              HTTPPriority priority) override;

  HTTPSettings* getEgressSettings() override;

  [[nodiscard]] const HTTPSettings* getIngressSettings() const override;

  void setHeaderCodecStats(HeaderCodec::Stats* stats) override;

  void enableDoubleGoawayDrain() override;

  void disableDoubleGoawayDrain() override;

  [[nodiscard]] HTTPCodec::StreamID getLastIncomingStreamID() const override;

  [[nodiscard]] uint32_t getDefaultWindowSize() const override;
};

class HTTPCodecFilterChain {
  using Chain = FilterChain<HTTPCodec,
                            HTTPCodec::Callback,
                            PassThroughHTTPCodecFilter,
                            &HTTPCodec::setCallback,
                            true>;

 public:
  using StreamID = HTTPCodec::StreamID;

  explicit HTTPCodecFilterChain(std::unique_ptr<HTTPCodec> codec)
      : chain_(std::move(codec)) {
  }

  HTTPCodecFilterChain(const HTTPCodecFilterChain&) = delete;
  HTTPCodecFilterChain& operator=(const HTTPCodecFilterChain&) = delete;
  HTTPCodecFilterChain(HTTPCodecFilterChain&&) = delete;
  HTTPCodecFilterChain& operator=(HTTPCodecFilterChain&&) = delete;
  ~HTTPCodecFilterChain() = default;

  [[nodiscard]] CodecProtocol getProtocol() const {
    return chain_->getProtocol();
  }

  [[nodiscard]] TransportDirection getTransportDirection() const {
    return chain_->getTransportDirection();
  }

  [[nodiscard]] bool supportsParallelRequests() const {
    return chain_->supportsParallelRequests();
  }

  [[nodiscard]] bool supportsSessionFlowControl() const {
    return chain_->supportsSessionFlowControl();
  }

  [[nodiscard]] bool supportsStreamFlowControl() const {
    return chain_->supportsStreamFlowControl();
  }

  void setParserPaused(bool paused) {
    chain_->setParserPaused(paused);
  }

  [[nodiscard]] const std::string& getUserAgent() const {
    return chain_->getUserAgent();
  }

  StreamID createStream() {
    return chain_->createStream();
  }

  [[nodiscard]] bool isBusy() const {
    return chain_->isBusy();
  }

  size_t onIngress(const folly::IOBuf& buf) {
    return chain_->onIngress(buf);
  }

  void onIngressEOF() {
    chain_->onIngressEOF();
  }

  [[nodiscard]] bool isReusable() const {
    return chain_->isReusable();
  }

  [[nodiscard]] bool isWaitingToDrain() const {
    return chain_->isWaitingToDrain();
  }

  [[nodiscard]] bool closeOnEgressComplete() const {
    return chain_->closeOnEgressComplete();
  }

  [[nodiscard]] bool supportsPushTransactions() const {
    return chain_->supportsPushTransactions();
  }

  size_t generateConnectionPreface(folly::IOBufQueue& writeBuf) {
    return chain_->generateConnectionPreface(writeBuf);
  }

  void generateHeader(
      folly::IOBufQueue& writeBuf,
      StreamID stream,
      const HTTPMessage& msg,
      bool eom = false,
      HTTPHeaderSize* size = nullptr,
      const folly::Optional<HTTPHeaders>& extraHeaders = folly::none) {
    chain_->generateHeader(writeBuf, stream, msg, eom, size, extraHeaders);
  }

  void generatePushPromise(folly::IOBufQueue& writeBuf,
                           StreamID stream,
                           const HTTPMessage& msg,
                           StreamID assocStream,
                           bool eom = false,
                           HTTPHeaderSize* size = nullptr) {
    chain_->generatePushPromise(writeBuf, stream, msg, assocStream, eom, size);
  }

  size_t generateBody(folly::IOBufQueue& writeBuf,
                      StreamID stream,
                      std::unique_ptr<folly::IOBuf> chain,
                      folly::Optional<uint8_t> padding,
                      bool eom) {
    return chain_->generateBody(
        writeBuf, stream, std::move(chain), padding, eom);
  }

  size_t generateChunkHeader(folly::IOBufQueue& writeBuf,
                             StreamID stream,
                             size_t length) {
    return chain_->generateChunkHeader(writeBuf, stream, length);
  }

  size_t generateChunkTerminator(folly::IOBufQueue& writeBuf, StreamID stream) {
    return chain_->generateChunkTerminator(writeBuf, stream);
  }

  size_t generateTrailers(folly::IOBufQueue& writeBuf,
                          StreamID stream,
                          const HTTPHeaders& trailers) {
    return chain_->generateTrailers(writeBuf, stream, trailers);
  }

  size_t generatePadding(folly::IOBufQueue& writeBuf,
                         StreamID stream,
                         uint16_t bytes) {
    return chain_->generatePadding(writeBuf, stream, bytes);
  }

  size_t generateEOM(folly::IOBufQueue& writeBuf, StreamID stream) {
    return chain_->generateEOM(writeBuf, stream);
  }

  size_t generateRstStream(folly::IOBufQueue& writeBuf,
                           StreamID stream,
                           ErrorCode code) {
    return chain_->generateRstStream(writeBuf, stream, code);
  }

  size_t generateGoaway(folly::IOBufQueue& writeBuf,
                        StreamID lastStream = HTTPCodec::MaxStreamID,
                        ErrorCode code = ErrorCode::NO_ERROR,
                        std::unique_ptr<folly::IOBuf> debugData = nullptr) {
    return chain_->generateGoaway(
        writeBuf, lastStream, code, std::move(debugData));
  }

  size_t generateImmediateGoaway(
      folly::IOBufQueue& writeBuf,
      ErrorCode code = ErrorCode::NO_ERROR,
      std::unique_ptr<folly::IOBuf> debugData = nullptr) {
    return chain_->generateImmediateGoaway(
        writeBuf, code, std::move(debugData));
  }

  size_t generatePingRequest(folly::IOBufQueue& writeBuf,
                             folly::Optional<uint64_t> data = folly::none) {
    return chain_->generatePingRequest(writeBuf, data);
  }

  size_t generatePingReply(folly::IOBufQueue& writeBuf, uint64_t data) {
    return chain_->generatePingReply(writeBuf, data);
  }

  size_t generateSettings(folly::IOBufQueue& writeBuf) {
    return chain_->generateSettings(writeBuf);
  }

  size_t generateSettingsAck(folly::IOBufQueue& writeBuf) {
    return chain_->generateSettingsAck(writeBuf);
  }

  size_t generateWindowUpdate(folly::IOBufQueue& writeBuf,
                              StreamID stream,
                              uint32_t delta) {
    return chain_->generateWindowUpdate(writeBuf, stream, delta);
  }

  size_t generatePriority(folly::IOBufQueue& writeBuf,
                          StreamID stream,
                          HTTPPriority priority) {
    return chain_->generatePriority(writeBuf, stream, priority);
  }

  size_t generatePushPriority(folly::IOBufQueue& writeBuf,
                              StreamID pushId,
                              HTTPPriority priority) {
    return chain_->generatePushPriority(writeBuf, pushId, priority);
  }

  [[nodiscard]] HTTPSettings* getEgressSettings() {
    return chain_->getEgressSettings();
  }

  [[nodiscard]] const HTTPSettings* getEgressSettings() const {
    return chain_->getEgressSettings();
  }

  [[nodiscard]] const HTTPSettings* getIngressSettings() const {
    return chain_->getIngressSettings();
  }

  [[nodiscard]] uint32_t getDefaultWindowSize() const {
    return chain_->getDefaultWindowSize();
  }

  void setHeaderCodecStats(HeaderCodec::Stats* stats) {
    chain_->setHeaderCodecStats(stats);
  }

  void enableDoubleGoawayDrain() {
    chain_->enableDoubleGoawayDrain();
  }

  void setCallback(HTTPCodec::Callback* callback) {
    chain_.setCallback(callback);
  }

  template <typename Filter, typename... Args>
  void add(Args&&... args) {
    chain_.add<Filter>(std::forward<Args>(args)...);
  }

  template <typename... Filters>
  void addFilters(Filters&&... filters) {
    chain_.addFilters(std::forward<Filters>(filters)...);
  }

  template <typename Fn>
  void foreach (Fn&& fn) {
    chain_.foreach (std::forward<Fn>(fn));
  }

  std::unique_ptr<HTTPCodec> setDestination(std::unique_ptr<HTTPCodec> dest) {
    return chain_.setDestination(std::move(dest));
  }

  HTTPCodec* call() {
    return chain_.call();
  }

  [[nodiscard]] const HTTPCodec& getChainEnd() const {
    return chain_.getChainEnd();
  }

  HTTPCodec* getChainEndPtr() {
    return chain_.getChainEndPtr();
  }

 private:
  Chain chain_;
};

} // namespace proxygen
