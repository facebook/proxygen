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

#include <bitset>
#include <boost/optional/optional.hpp>
#include <deque>
#include <proxygen/lib/http/HTTPHeaders.h>
#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/http/codec/HTTPParallelCodec.h>
#include <proxygen/lib/http/codec/HTTPSettings.h>
#include <proxygen/lib/http/codec/SPDYConstants.h>
#include <proxygen/lib/http/codec/SPDYVersionSettings.h>
#include <proxygen/lib/http/codec/compress/HPACKCodec.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <zlib.h>

namespace folly { namespace io {
class Cursor;
}}

namespace proxygen {

/**
 * An implementation of the framing layer for all versions of
 * SPDY. Instances of this class must not be used from multiple threads
 * concurrently.
 */
class SPDYCodec: public HTTPParallelCodec {
public:
  explicit SPDYCodec(TransportDirection direction,
                     SPDYVersion version,
                     int spdyCompressionLevel = Z_NO_COMPRESSION);
  ~SPDYCodec() override;

  static const SPDYVersionSettings& getVersionSettings(SPDYVersion version);

  // HTTPCodec API
  CodecProtocol getProtocol() const override;
  bool supportsStreamFlowControl() const override;
  bool supportsSessionFlowControl() const override;
  size_t onIngress(const folly::IOBuf& buf) override;
  bool supportsPushTransactions() const override { return true; }
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
                           StreamID txn,
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
  size_t generateWindowUpdate(folly::IOBufQueue& writeBuf,
                              StreamID stream,
                              uint32_t delta) override;

  /**
   * Returns a const reference to the ingress settings. Since ingress
   * settings are set by the remote end, it doesn't make sense for these
   * to be mutable outside the codec.
   */
  const HTTPSettings* getIngressSettings() const override {
    return &ingressSettings_;
  }
  /**
   * Returns a reference to the egress settings
   */
  HTTPSettings* getEgressSettings() override { return &egressSettings_; }
  uint32_t getDefaultWindowSize() const override {
    return spdy::kInitialWindow;
  }

  uint8_t getVersion() const;

  uint8_t getMinorVersion() const;

  void setMaxFrameLength(uint32_t maxFrameLength);

  /**
   * Set the maximum size of the uncompressed headers
   */
  void setMaxUncompressedHeaders(uint32_t maxUncompressed);

  void setHeaderCodecStats(HeaderCodec::Stats* stats) override {
    headerCodec_->setStats(stats);
  }

  size_t addPriorityNodes(
      PriorityQueue& queue,
      folly::IOBufQueue& writeBuf,
      uint8_t maxLevel) override;

  StreamID mapPriorityToDependency(uint8_t priority) const override {
    return MAX_STREAM_ID + priority;
  }

  int8_t mapDependencyToPriority(StreamID parent) const override {
    if (parent >= MAX_STREAM_ID) {
      return parent - MAX_STREAM_ID;
    }
    return -1;
  }

  struct SettingData {
    SettingData(uint8_t inFlags, uint32_t inId, uint32_t inValue)
        : flags(inFlags),
          id(inId),
          value(inValue) {}
    uint8_t flags;
    uint32_t id;
    uint32_t value;
  };

  typedef std::vector<SettingData> SettingList;

  /**
   * Returns the SPDYVersion for the given protocol string, or none otherwise.
   */
  static boost::optional<SPDYVersion> getVersion(const std::string& protocol);

 private:

  /**
   * Generates a frame of type SYN_STREAM
   */
  void generateSynStream(StreamID stream,
                         StreamID assocStream,
                         folly::IOBufQueue& writeBuf,
                         const HTTPMessage& msg,
                         bool eom,
                         HTTPHeaderSize* size);
  /**
   * Generates a frame of type SYN_REPLY
   */
  void generateSynReply(StreamID stream,
                        folly::IOBufQueue& writeBuf,
                        const HTTPMessage& msg,
                        bool eom,
                        HTTPHeaderSize* size);

  /**
   * Generates the shared parts of a ping request and reply.
   */
  size_t generatePingCommon(folly::IOBufQueue& writeBuf,
                            uint64_t uniqueID);
  /**
   * Ingress parser, can throw exceptions
   */
  size_t parseIngress(const folly::IOBuf& buf);

  /**
   * Handle an ingress SYN_STREAM control frame. For a downstream-facing
   * SPDY session, this frame is the equivalent of an HTTP request header.
   */
  void onSynStream(uint32_t assocStream,
                   uint8_t pri, uint8_t slot,
                   const compress::HeaderPieceList& headers,
                   const HTTPHeaderSize& size);
  /**
   * Handle an ingress SYN_REPLY control frame. For an upstream-facing
   * SPDY session, this frame is the equivalent of an HTTP response header.
   */
  void onSynReply(const compress::HeaderPieceList& headers,
                  const HTTPHeaderSize& size);
  /**
   * Handle an ingress RST_STREAM control frame.
   */
  void onRstStream(uint32_t statusCode) noexcept;
  /**
   * Handle a SETTINGS message that changes/updates settings for the
   * entire SPDY connection (across all transactions)
   */
  void onSettings(const SettingList& settings);

  void onPing(uint32_t uniqueID) noexcept;

  void onGoaway(uint32_t lastGoodStream,
                uint32_t statusCode) noexcept;
  /**
   * Handle a HEADERS frame. This is *not* invoked when the first headers
   * on a stream are received. This is called when the remote endpoint
   * sends us any additional headers.
   */
  void onHeaders(const compress::HeaderPieceList& headers) noexcept;

  void onWindowUpdate(uint32_t delta) noexcept;

  // Helpers

  /**
   * Parses the headers in the nameValues array and creates an HTTPMessage
   * object initialized for this transaction.
   */
  std::unique_ptr<HTTPMessage> parseHeaders(
    TransportDirection direction, StreamID streamID,
    StreamID assocStreamID, const compress::HeaderPieceList& headers);

  /**
   * Helper function to parse out a control frame and execute its handler.
   * All errors are thrown as exceptions.
   */
  void onControlFrame(folly::io::Cursor& cursor);

  /**
   * Helper function that contains the common implementation details of
   * calling the same callbacks for onSynStream() and onSynReply()
   *
   * Negative values of pri are interpreted much like negative array
   * indexes in python, so -1 will be the largest numerical priority
   * value for this SPDY version (i.e., 3 for SPDY/2 or 7 for SPDY/3),
   * -2 the second largest (i.e., 2 for SPDY/2 or 6 for SPDY/3).
   */
  void onSynCommon(StreamID streamID,
                   StreamID assocStreamID,
                   const compress::HeaderPieceList& headers,
                   int8_t pri,
                   const HTTPHeaderSize& size);

  void deliverOnMessageBegin(StreamID streamID, StreamID assocStreamID,
                             HTTPMessage* msg);

  /**
   * Generate the header for a SPDY data frame
   * @param writeBuf Buffer queue to which the control frame is written.
   * @param streamID Stream ID.
   * @param flags    Bitmap of flags, as defined in the SPDY spec.
   * @param length   Length of the data, in bytes.
   * @return length  Length of the encoded bytes
   * @return payload data payload
   */
  size_t generateDataFrame(folly::IOBufQueue& writeBuf,
                           uint32_t streamID,
                           uint8_t flags,
                           uint32_t length,
                           std::unique_ptr<folly::IOBuf> payload);

  /**
   * Serializes headers for requests (aka SYN_STREAM)
   * @param msg      The message to serialize.
   * @param isPushed true if this is a push message
   * @param size     Size of the serialized headers before and after compression
   * @param headroom Optional amount of headroom to reserve at the
   *                 front of the returned IOBuf, in case the caller
   *                 wants to put some other data there.
   */
  std::unique_ptr<folly::IOBuf> serializeRequestHeaders(
    const HTTPMessage& msg,
    bool isPushed,
    uint32_t headroom = 0,
    HTTPHeaderSize* size = nullptr);

  /**
   * Serializes headers for responses (aka SYN_REPLY)
   * @param msg      The message to serialize.
   * @param size     Size of the serialized headers before and after compression
   * @param headroom Optional amount of headroom to reserve at the
   *                 front of the returned IOBuf, in case the caller
   *                 wants to put some other data there.
   */
  std::unique_ptr<folly::IOBuf> serializeResponseHeaders(
     const HTTPMessage& msg,
     uint32_t headroom = 0,
     HTTPHeaderSize* size = nullptr);

  /**
   * Helper function to create the compressed Name/Value representation of
   * a message's headers.
   * @param msg      The message containing headers to serialize.
   * @param headers  A vector containing any extra headers to serialize
   * @param size     Size of the serialized headers before and after compression
   * @param headroom Optional amount of headroom to reserve at the
   *                 front of the returned IOBuf, in case the caller
   *                 wants to put some other data there.
   */
  std::unique_ptr<folly::IOBuf> encodeHeaders(
    const HTTPMessage& msg, std::vector<compress::Header>& headers,
    uint32_t headroom = 0,
    HTTPHeaderSize* size = nullptr);

  void failStream(bool newTxn, StreamID streamID, uint32_t code,
                  std::string excStr = empty_string);

  void failSession(uint32_t statusCode);

  /**
   * Decodes the headers from the cursor and returns the result.
   */
  HeaderDecodeResult decodeHeaders(folly::io::Cursor& cursor);

  void checkLength(uint32_t expectedLength, const std::string& msg);

  void checkMinLength(uint32_t minLength, const std::string& msg);

  bool isSPDYReserved(const std::string& name);

  /**
   * Helper function to check if the status code is supported by the
   * SPDY version being used
   */
  bool rstStatusSupported(int statusCode) const;

  folly::fbvector<StreamID> closedStreams_;
  const SPDYVersionSettings& versionSettings_;

  HTTPSettings ingressSettings_{
    {SettingsId::MAX_CONCURRENT_STREAMS, spdy::kMaxConcurrentStreams},
    {SettingsId::INITIAL_WINDOW_SIZE, spdy::kInitialWindow}
  };
  HTTPSettings egressSettings_{
    {SettingsId::MAX_CONCURRENT_STREAMS, spdy::kMaxConcurrentStreams},
    {SettingsId::INITIAL_WINDOW_SIZE, spdy::kInitialWindow}
  };

  std::unique_ptr<HTTPMessage> partialMsg_;
  const folly::IOBuf* currentIngressBuf_{nullptr};

  StreamID nextEgressPingID_;
  // StreamID's are 31 bit unsigned integers, so all received goaways will
  // be lower than this.

  uint32_t maxFrameLength_{spdy::kMaxFrameLength};
  uint32_t streamId_{0};
  uint32_t length_{0};
  uint16_t version_{0};
  uint16_t type_{0xffff};
  uint8_t flags_{0};

  // SPDY Frame parsing state
  enum FrameState {
    FRAME_HEADER = 0,
    CTRL_FRAME_DATA = 1,
    DATA_FRAME_DATA = 2,
  } frameState_:2;

  bool ctrl_:1;

  std::unique_ptr<HeaderCodec> headerCodec_;
};

} // proxygen
