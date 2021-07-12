/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/GMock.h>
#include <proxygen/lib/http/codec/HTTPCodec.h>

namespace proxygen {

#if defined(__clang__) && __clang_major__ >= 3 && __clang_minor__ >= 6
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#endif

class MockHTTPCodec : public HTTPCodec {
 public:
  MOCK_CONST_METHOD0(getProtocol, CodecProtocol());
  MOCK_CONST_METHOD0(getUserAgent, const std::string&());
  MOCK_CONST_METHOD0(getTransportDirection, TransportDirection());
  MOCK_CONST_METHOD0(supportsStreamFlowControl, bool());
  MOCK_CONST_METHOD0(supportsSessionFlowControl, bool());
  MOCK_METHOD0(createStream, HTTPCodec::StreamID());
  MOCK_METHOD1(setCallback, void(Callback*));
  MOCK_CONST_METHOD0(isBusy, bool());
  MOCK_CONST_METHOD0(hasPartialTransaction, bool());
  MOCK_METHOD1(setParserPaused, void(bool));
  MOCK_CONST_METHOD0(isParserPaused, bool());
  MOCK_METHOD1(onIngress, size_t(const folly::IOBuf&));
  MOCK_METHOD0(onIngressEOF, void());
  MOCK_CONST_METHOD0(isReusable, bool());
  MOCK_CONST_METHOD0(isWaitingToDrain, bool());
  MOCK_CONST_METHOD0(closeOnEgressComplete, bool());
  MOCK_CONST_METHOD0(supportsParallelRequests, bool());
  MOCK_CONST_METHOD0(supportsPushTransactions, bool());
  MOCK_METHOD6(generateHeader,
               void(folly::IOBufQueue&,
                    HTTPCodec::StreamID,
                    const HTTPMessage&,
                    bool eom,
                    HTTPHeaderSize*,
                    const folly::Optional<HTTPHeaders>&));
  MOCK_METHOD6(generatePushPromise,
               void(folly::IOBufQueue&,
                    HTTPCodec::StreamID,
                    const HTTPMessage&,
                    HTTPCodec::StreamID,
                    bool eom,
                    HTTPHeaderSize*));
  MOCK_METHOD5(generateBody,
               size_t(folly::IOBufQueue&,
                      HTTPCodec::StreamID,
                      std::shared_ptr<folly::IOBuf>,
                      folly::Optional<uint8_t>,
                      bool));
  size_t generateBody(folly::IOBufQueue& writeBuf,
                      HTTPCodec::StreamID stream,
                      std::unique_ptr<folly::IOBuf> chain,
                      folly::Optional<uint8_t> padding,
                      bool eom) override {
    return generateBody(writeBuf,
                        stream,
                        std::shared_ptr<folly::IOBuf>(chain.release()),
                        padding,
                        eom);
  }
  MOCK_METHOD3(generateChunkHeader,
               size_t(folly::IOBufQueue&, HTTPCodec::StreamID, size_t));
  MOCK_METHOD2(generateChunkTerminator,
               size_t(folly::IOBufQueue&, HTTPCodec::StreamID));
  MOCK_METHOD3(generateTrailers,
               size_t(folly::IOBufQueue&,
                      HTTPCodec::StreamID,
                      const HTTPHeaders&));
  MOCK_METHOD2(generateEOM, size_t(folly::IOBufQueue&, HTTPCodec::StreamID));
  MOCK_METHOD3(generateRstStream,
               size_t(folly::IOBufQueue&, HTTPCodec::StreamID, ErrorCode));
  MOCK_METHOD4(generateGoaway,
               size_t(folly::IOBufQueue&,
                      StreamID,
                      ErrorCode,
                      std::shared_ptr<folly::IOBuf>));
  size_t generateGoaway(folly::IOBufQueue& writeBuf,
                        StreamID lastStream,
                        ErrorCode statusCode,
                        std::unique_ptr<folly::IOBuf> debugData) override {
    return generateGoaway(writeBuf,
                          lastStream,
                          statusCode,
                          std::shared_ptr<folly::IOBuf>(debugData.release()));
  }

  MOCK_METHOD1(generatePingRequest, size_t(folly::IOBufQueue&));
  size_t generatePingRequest(folly::IOBufQueue& writeBuf,
                             folly::Optional<uint64_t> /* data */) override {
    return generatePingRequest(writeBuf);
  }

  MOCK_METHOD2(generatePingReply, size_t(folly::IOBufQueue&, uint64_t));
  MOCK_METHOD1(generateSettings, size_t(folly::IOBufQueue&));
  MOCK_METHOD1(generateSettingsAck, size_t(folly::IOBufQueue&));
  MOCK_METHOD3(generateWindowUpdate,
               size_t(folly::IOBufQueue&, StreamID, uint32_t));
  MOCK_METHOD3(generateCertificateRequest,
               size_t(folly::IOBufQueue&,
                      uint16_t,
                      std::shared_ptr<folly::IOBuf>));
  size_t generateCertificateRequest(
      folly::IOBufQueue& writeBuf,
      uint16_t requestId,
      std::unique_ptr<folly::IOBuf> authRequest) override {
    return generateCertificateRequest(
        writeBuf,
        requestId,
        std::shared_ptr<folly::IOBuf>(authRequest.release()));
  }
  MOCK_METHOD3(generateCertificate,
               size_t(folly::IOBufQueue&,
                      uint16_t,
                      std::shared_ptr<folly::IOBuf>));
  size_t generateCertificate(
      folly::IOBufQueue& writeBuf,
      uint16_t certId,
      std::unique_ptr<folly::IOBuf> authenticator) override {
    return generateCertificate(
        writeBuf,
        certId,
        std::shared_ptr<folly::IOBuf>(authenticator.release()));
  }
  MOCK_METHOD0(getEgressSettings, HTTPSettings*());
  MOCK_CONST_METHOD0(getIngressSettings, const HTTPSettings*());
  MOCK_METHOD0(enableDoubleGoawayDrain, void());
  MOCK_CONST_METHOD0(getDefaultWindowSize, uint32_t());
  MOCK_METHOD3(addPriorityNodes,
               size_t(PriorityQueue&, folly::IOBufQueue&, uint8_t));
  MOCK_CONST_METHOD1(mapPriorityToDependency, HTTPCodec::StreamID(uint8_t));
};

class MockHTTPCodecCallback : public HTTPCodec::Callback {
 public:
  MOCK_METHOD2(onMessageBegin, void(HTTPCodec::StreamID, HTTPMessage*));
  MOCK_METHOD3(onPushMessageBegin,
               void(HTTPCodec::StreamID, HTTPCodec::StreamID, HTTPMessage*));
  MOCK_METHOD4(
      onExMessageBegin,
      void(HTTPCodec::StreamID, HTTPCodec::StreamID, bool, HTTPMessage*));
  MOCK_METHOD2(onHeadersComplete,
               void(HTTPCodec::StreamID, std::shared_ptr<HTTPMessage>));
  void onHeadersComplete(HTTPCodec::StreamID stream,
                         std::unique_ptr<HTTPMessage> msg) override {
    onHeadersComplete(stream, std::shared_ptr<HTTPMessage>(msg.release()));
  }
  MOCK_METHOD3(onBody,
               void(HTTPCodec::StreamID,
                    std::shared_ptr<folly::IOBuf>,
                    uint8_t));
  void onBody(HTTPCodec::StreamID stream,
              std::unique_ptr<folly::IOBuf> chain,
              uint16_t padding) override {
    onBody(stream, std::shared_ptr<folly::IOBuf>(chain.release()), padding);
  }
  MOCK_METHOD2(onChunkHeader, void(HTTPCodec::StreamID, size_t));
  MOCK_METHOD1(onChunkComplete, void(HTTPCodec::StreamID));
  MOCK_METHOD2(onTrailersComplete,
               void(HTTPCodec::StreamID, std::shared_ptr<HTTPHeaders>));
  void onTrailersComplete(HTTPCodec::StreamID stream,
                          std::unique_ptr<HTTPHeaders> trailers) override {
    onTrailersComplete(stream,
                       std::shared_ptr<HTTPHeaders>(trailers.release()));
  }
  MOCK_METHOD2(onMessageComplete, void(HTTPCodec::StreamID, bool));
  MOCK_METHOD3(onError,
               void(HTTPCodec::StreamID, std::shared_ptr<HTTPException>, bool));
  void onError(HTTPCodec::StreamID stream,
               const HTTPException& exc,
               bool newStream) override {
    onError(stream,
            std::shared_ptr<HTTPException>(new HTTPException(exc)),
            newStream);
  }
  MOCK_METHOD5(onFrameHeader,
               void(uint64_t, uint8_t, uint64_t, uint64_t, uint16_t));
  MOCK_METHOD2(onAbort, void(HTTPCodec::StreamID, ErrorCode));
  MOCK_METHOD3(onGoaway,
               void(uint64_t, ErrorCode, std::shared_ptr<folly::IOBuf>));
  void onGoaway(uint64_t lastGoodStreamID,
                ErrorCode code,
                std::unique_ptr<folly::IOBuf> debugData) override {
    onGoaway(lastGoodStreamID,
             code,
             std::shared_ptr<folly::IOBuf>(debugData.release()));
  }
  MOCK_METHOD2(onUnknownFrame, void(uint64_t, uint64_t));
  MOCK_METHOD1(onPingRequest, void(uint64_t));
  MOCK_METHOD1(onPingReply, void(uint64_t));
  MOCK_METHOD2(onWindowUpdate, void(HTTPCodec::StreamID, uint32_t));
  MOCK_METHOD1(onSettings, void(const SettingsList&));
  MOCK_METHOD0(onSettingsAck, void());
  MOCK_METHOD2(onPriority,
               void(HTTPCodec::StreamID, const HTTPMessage::HTTP2Priority&));
  MOCK_METHOD2(onPriority, void(HTTPCodec::StreamID, const HTTPPriority&));
  MOCK_METHOD2(onCertificateRequest,
               void(uint16_t, std::shared_ptr<folly::IOBuf>));
  void onCertificateRequest(
      uint16_t requestId,
      std::unique_ptr<folly::IOBuf> certRequestData) override {
    onCertificateRequest(
        requestId, std::shared_ptr<folly::IOBuf>(certRequestData.release()));
  }
  MOCK_METHOD2(onCertificate, void(uint16_t, std::shared_ptr<folly::IOBuf>));
  void onCertificate(uint16_t certId,
                     std::unique_ptr<folly::IOBuf> certData) override {
    onCertificate(certId, std::shared_ptr<folly::IOBuf>(certData.release()));
  }
  MOCK_METHOD4(onNativeProtocolUpgrade,
               bool(HTTPCodec::StreamID,
                    CodecProtocol,
                    const std::string&,
                    HTTPMessage&));
  MOCK_METHOD4(onGenerateFrameHeader,
               void(HTTPCodec::StreamID, uint8_t, uint64_t, uint16_t));
  MOCK_CONST_METHOD0(numOutgoingStreams, uint32_t());
  MOCK_CONST_METHOD0(numIncomingStreams, uint32_t());
};

#if defined(__clang__) && __clang_major__ >= 3 && __clang_minor__ >= 6
#pragma clang diagnostic pop
#endif

} // namespace proxygen
