/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/http/codec/TransportDirection.h>
#include <string>

#include <proxygen/external/http_parser/http_parser.h>

namespace proxygen {

class HTTP1xCodec : public HTTPCodec {
 public:
  explicit HTTP1xCodec(TransportDirection direction,
                       bool forceUpstream1_1 = false);
  ~HTTP1xCodec() override;

  // HTTPCodec API
  CodecProtocol getProtocol() const override {
    return CodecProtocol::HTTP_1_1;
  }
  TransportDirection getTransportDirection() const override {
    return transportDirection_;
  }
  StreamID createStream() override;
  void setCallback(Callback* callback) override { callback_ = callback; }
  bool isBusy() const override;
  void setParserPaused(bool paused) override;
  size_t onIngress(const folly::IOBuf& buf) override;
  void onIngressEOF() override;
  bool isReusable() const override;
  bool isWaitingToDrain() const override { return false; }
  // If the session has been upgraded we will send EOF (or RST if needed)
  // on egress complete
  bool closeOnEgressComplete() const override { return egressUpgrade_; }
  bool supportsParallelRequests() const override { return false; }
  bool supportsPushTransactions() const override { return false; }
  void generateHeader(folly::IOBufQueue& writeBuf,
                      StreamID txn,
                      const HTTPMessage& msg,
                      StreamID assocStream = 0,
                      bool eom = false,
                      HTTPHeaderSize* size = nullptr) override;
  size_t generateBody(folly::IOBufQueue& writeBuf,
                      StreamID txn,
                      std::unique_ptr<folly::IOBuf> chain,
                      boost::optional<uint8_t> padding,
                      bool eom) override;
  size_t generateChunkHeader(folly::IOBufQueue& writeBuf,
                             StreamID txn,
                             size_t length) override;
  size_t generateChunkTerminator(folly::IOBufQueue& writeBuf,
                                 StreamID txn) override;
  size_t generateTrailers(folly::IOBufQueue& writeBuf,
                          StreamID txn,
                          const HTTPHeaders& trailers) override;
  size_t generateEOM(folly::IOBufQueue& writeBuf,
                     StreamID txn) override;
  size_t generateRstStream(folly::IOBufQueue& writeBuf,
                           StreamID txn,
                           ErrorCode statusCode) override;
  size_t generateGoaway(
    folly::IOBufQueue& writeBuf,
    StreamID lastStream,
    ErrorCode statusCode,
    std::unique_ptr<folly::IOBuf> debugData = nullptr) override;

  void setAllowedUpgradeProtocols(std::list<std::string> protocols);

  /**
   * @returns true if the codec supports the given NPN protocol.
   */
  static bool supportsNextProtocol(const std::string& npn);

 private:
  /** Simple state model used to track the parsing of HTTP headers */
  enum class HeaderParseState : uint8_t {
    kParsingHeaderIdle,
    kParsingHeaderStart,
    kParsingHeaderName,
    kParsingHeaderValue,
    kParsingHeadersComplete,
    kParsingTrailerName,
    kParsingTrailerValue
  };

  /** Used to keep track of whether a client requested keep-alive. This is
   * only useful to support HTTP 1.0 keep-alive for a downstream connection
   * where keep-alive is disabled unless the client requested it. */
  enum class KeepaliveRequested : uint8_t {
    UNSET,
    ENABLED,  // incoming message requested keepalive
    DISABLED,   // incoming message disabled keepalive
  };

  void addDateHeader(folly::IOBufQueue& writeBuf, size_t& len);

  /** Check whether we're currently parsing ingress message headers */
  bool isParsingHeaders() const {
    return (headerParseState_ > HeaderParseState::kParsingHeaderIdle) &&
       (headerParseState_ < HeaderParseState::kParsingHeadersComplete);
  }

  /** Check whether we're currently parsing ingress header-or-trailer name */
  bool isParsingHeaderOrTrailerName() const {
    return (headerParseState_ == HeaderParseState::kParsingHeaderName) ||
        (headerParseState_ == HeaderParseState::kParsingTrailerName);
  }

  /** Invoked when a parsing error occurs. It will send an exception to
      the callback object to report the error and do any other cleanup
      needed. It optionally takes a message to pass to the generated
      HTTPException passed to callback_. */
  void onParserError(const char* what = nullptr);

  /** Push out header name-value pair to hdrs and clear currentHeader*_ */
  void pushHeaderNameAndValue(HTTPHeaders& hdrs);

  // Parser callbacks
  int onMessageBegin();
  int onURL(const char* buf, size_t len);
  int onReason(const char* buf, size_t len);
  int onHeaderField(const char* buf, size_t len);
  int onHeaderValue(const char* buf, size_t len);
  int onHeadersComplete(size_t len);
  int onBody(const char* buf, size_t len);
  int onChunkHeader(size_t len);
  int onChunkComplete();
  int onMessageComplete();

  HTTPCodec::Callback* callback_;
  StreamID ingressTxnID_;
  StreamID egressTxnID_;
  http_parser parser_;
  const folly::IOBuf* currentIngressBuf_;
  std::unique_ptr<HTTPMessage> msg_;
  std::unique_ptr<HTTPMessage> upgradeRequest_;
  std::unique_ptr<HTTPHeaders> trailers_;
  std::string currentHeaderName_;
  folly::StringPiece currentHeaderNameStringPiece_;
  std::string currentHeaderValue_;
  std::string url_;
  std::string reason_;
  std::string upgradeHeader_; // last sent/received client upgrade header
  std::string allowedNativeUpgrades_; // DOWNSTREAM only
  HTTPHeaderSize headerSize_;
  HeaderParseState headerParseState_;
  TransportDirection transportDirection_;
  KeepaliveRequested keepaliveRequested_; // only used in DOWNSTREAM mode
  std::pair<CodecProtocol, std::string> upgradeResult_; // DOWNSTREAM only
  bool forceUpstream1_1_:1; // Use HTTP/1.1 upstream even if msg is 1.0
  bool parserActive_:1;
  bool pendingEOF_:1;
  bool parserPaused_:1;
  bool parserError_:1;
  bool requestPending_:1;
  bool responsePending_:1;
  bool egressChunked_:1;
  bool inChunk_:1;
  bool lastChunkWritten_:1;
  bool keepalive_:1;
  bool disableKeepalivePending_:1;
  // TODO: replace the 2 booleans below with an enum "request method"
  bool connectRequest_:1;
  bool headRequest_:1;
  bool expectNoResponseBody_:1;
  bool mayChunkEgress_:1;
  bool is1xxResponse_:1;
  bool inRecvLastChunk_:1;
  bool ingressUpgrade_:1;
  bool ingressUpgradeComplete_:1;
  bool egressUpgrade_:1;
  bool nativeUpgrade_:1;
  bool headersComplete_:1;

  // C-callable wrappers for the http_parser callbacks
  static int onMessageBeginCB(http_parser* parser);
  static int onPathCB(http_parser* parser, const char* buf, size_t len);
  static int onQueryStringCB(http_parser* parser, const char* buf, size_t len);
  static int onUrlCB(http_parser* parser, const char* buf, size_t len);
  static int onReasonCB(http_parser* parser, const char* buf, size_t len);
  static int onHeaderFieldCB(http_parser* parser, const char* buf, size_t len);
  static int onHeaderValueCB(http_parser* parser, const char* buf, size_t len);
  static int onHeadersCompleteCB(http_parser* parser,
                                 const char* buf, size_t len);
  static int onBodyCB(http_parser* parser, const char* buf, size_t len);
  static int onChunkHeaderCB(http_parser* parser);
  static int onChunkCompleteCB(http_parser* parser);
  static int onMessageCompleteCB(http_parser* parser);

  static const http_parser_settings* getParserSettings();
};

} // proxygen
