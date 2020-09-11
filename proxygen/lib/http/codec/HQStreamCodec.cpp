/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/HQStreamCodec.h>
#include <folly/Format.h>
#include <folly/ScopeGuard.h>
#include <folly/SingletonThreadLocal.h>
#include <folly/io/Cursor.h>
#include <proxygen/lib/http/HTTP3ErrorCode.h>
#include <proxygen/lib/http/codec/HQUtils.h>
#include <proxygen/lib/http/codec/compress/QPACKCodec.h>

namespace proxygen { namespace hq {

using namespace folly;
using namespace folly::io;

using std::string;

HQStreamCodec::HQStreamCodec(StreamID streamId,
                             TransportDirection direction,
                             QPACKCodec& headerCodec,
                             folly::IOBufQueue& encoderWriteBuf,
                             folly::IOBufQueue& decoderWriteBuf,
                             folly::Function<uint64_t()> qpackEncoderMaxData,
                             HTTPSettings& egressSettings,
                             HTTPSettings& ingressSettings,
                             bool transportSupportsPartialReliability)
    : HQFramedCodec(streamId, direction),
      headerCodec_(headerCodec),
      qpackEncoderWriteBuf_(encoderWriteBuf),
      qpackDecoderWriteBuf_(decoderWriteBuf),
      qpackEncoderMaxDataFn_(std::move(qpackEncoderMaxData)),
      egressSettings_(egressSettings),
      ingressSettings_(ingressSettings),
      transportSupportsPartialReliability_(
          transportSupportsPartialReliability) {
  VLOG(4) << "creating " << getTransportDirectionString(direction)
          << " HQ stream codec for stream " << streamId_;
}

HQStreamCodec::~HQStreamCodec() {
}

ParseResult HQStreamCodec::checkFrameAllowed(FrameType type) {
  switch (type) {
    case hq::FrameType::SETTINGS:
    case hq::FrameType::GOAWAY:
    case hq::FrameType::MAX_PUSH_ID:
    case hq::FrameType::CANCEL_PUSH:
      return HTTP3::ErrorCode::HTTP_WRONG_STREAM;
    case hq::FrameType::PUSH_PROMISE:
      if (transportDirection_ == TransportDirection::DOWNSTREAM) {
        return HTTP3::ErrorCode::HTTP_WRONG_STREAM;
      }
    default:
      break;
  }
  return folly::none;
}

ParseResult HQStreamCodec::parseData(Cursor& cursor,
                                     const FrameHeader& header) {
  // It's possible the data is in the wrong place per HTTP semantics, but it
  // will be caught by HTTPTransaction
  std::unique_ptr<IOBuf> outData;
  VLOG(10) << "parsing all frame DATA bytes for stream=" << streamId_
           << " length=" << header.length;
  auto res = hq::parseData(cursor, header, outData);
  if (res) {
    return res;
  }

  // no need to do deliverCallbackIfAllowed
  // the HQSession can trap this and stop reading.
  // i.e we can immediately reset in onNewStream if we get a stream id
  // higher than MAXID advertised in the goaway
  if (callback_ && (outData && !outData->empty())) {
    callback_->onBody(streamId_, std::move(outData), 0);
  }
  return res;
}

void HQStreamCodec::onIngressPartiallyReliableBodyStarted(
    uint64_t streamOffset) {
  CHECK(transportSupportsPartialReliability())
      << __func__
      << ": partially reliable operation on non-partially reliable transport";

  CHECK(!ingressPrBodyTracker_.bodyStarted())
      << ": partially realible body tracker already started";

  // Starts tracking the body byte offsets.
  // streamOffset becomes the beginning (0-offset) of the body offset.
  ingressPrBodyTracker_.startBodyTracking(streamOffset);

  ingressPartiallyReliable_ = true;

  if (callback_) {
    callback_->onUnframedBodyStarted(streamId_, streamOffset);
  }
}

ParseResult HQStreamCodec::parsePartiallyReliableData(Cursor& cursor) {
  CHECK(transportSupportsPartialReliability())
      << __func__
      << ": partially reliable operation on non-partially reliable transport";

  std::unique_ptr<IOBuf> outData;
  auto dataLength = cursor.totalLength();

  VLOG(10) << "parsing all frame DATA bytes for stream=" << streamId_
           << " length=" << dataLength;

  DCHECK(ingressPrBodyTracker_.bodyStarted());
  if (!ingressPrBodyTracker_.bodyStarted()) {
    LOG(ERROR) << __func__ << ": body hasn't started yet";
    return HTTP3::ErrorCode::HTTP_GENERAL_PROTOCOL_ERROR;
  }

  cursor.clone(outData, dataLength);

  ingressPrBodyTracker_.addBodyBytesProcessed(dataLength);

  if (callback_ && outData && !outData->empty()) {
    callback_->onBody(streamId_, std::move(outData), 0 /* padding */);
  }
  return folly::none;
}

ParseResult HQStreamCodec::parseHeaders(Cursor& cursor,
                                        const FrameHeader& header) {
  if (finalIngressHeadersSeen_) {
    // No Trailers for YOU!
    if (callback_) {
      HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                       "Invalid HEADERS frame");
      ex.setErrno(uint32_t(HTTP3::ErrorCode::HTTP_UNEXPECTED_FRAME));
      callback_->onError(streamId_, ex, false);
    }
    setParserPaused(true);
    return folly::none;
  }
  std::unique_ptr<IOBuf> outHeaderData;
  auto res = hq::parseHeaders(cursor, header, outHeaderData);
  if (res) {
    return res;
  }
  if (callback_) {
    // H2 performs the decompression/semantic validation first.
    // Also, this should really only be called once per this whole codec, not
    // per header block -- think info status, and (shudder) trailers. This
    // behavior mirrors HTTP2Codec at present.
    callback_->onMessageBegin(streamId_, nullptr);
  }
  // TODO: Handle HTTP trailers (T35711545)
  decodeInfo_.init(transportDirection_ == TransportDirection::DOWNSTREAM,
                   false /* isRequestTrailers */);
  headerCodec_.decodeStreaming(
      streamId_, std::move(outHeaderData), header.length, this);
  // decodeInfo_.msg gets moved in onHeadersComplete.  If it is still around,
  // parsing is incomplete, and needs to be paused.
  if (decodeInfo_.msg) {
    setParserPaused(true);
  }
  return res;
}

ParseResult HQStreamCodec::parsePushPromise(Cursor& cursor,
                                            const FrameHeader& header) {
  PushId outPushId;
  std::unique_ptr<IOBuf> outHeaderData;
  auto res = hq::parsePushPromise(cursor, header, outPushId, outHeaderData);
  if (res) {
    return res;
  }

  // Notify the callback on beginning of a push promise.
  // The callback will be further notified when the header block
  // is fully parsed, via a call to `onHeadersComplete`.
  // It is up to the callback to match the push promise
  // with the headers block, via using same stream id
  if (callback_) {
    callback_->onPushMessageBegin(outPushId, streamId_, nullptr);
  }

  decodeInfo_.init(true /* isReq */, false /* isRequestTrailers */);
  auto headerDataLength = outHeaderData->computeChainDataLength();
  headerCodec_.decodeStreaming(
      streamId_, std::move(outHeaderData), headerDataLength, this);
  if (decodeInfo_.msg) {
    // parsing incomplete, see comment in parseHeaders
    setParserPaused(true);
  }
  return res;
}

void HQStreamCodec::onHeader(const HPACKHeaderName& name,
                             const folly::fbstring& value) {
  if (decodeInfo_.onHeader(name, value)) {
    if (userAgent_.empty() && name.getHeaderCode() == HTTP_HEADER_USER_AGENT) {
      userAgent_ = value.toStdString();
    }
  } else {
    VLOG(4) << "dir=" << uint32_t(transportDirection_)
            << decodeInfo_.parsingError << " codec=" << headerCodec_;
  }
}

folly::Expected<uint64_t, UnframedBodyOffsetTrackerError>
HQStreamCodec::onIngressDataAvailable(uint64_t streamOffset) {
  CHECK(transportSupportsPartialReliability())
      << __func__
      << ": partially reliable operation on non-partially reliable transport";

  if (!finalIngressHeadersSeen_ || !ingressPrBodyTracker_.bodyStarted()) {
    // Do not raise onDataAvailable() before body starts.
    VLOG(4) << ": suppressing " << __func__
            << " because body did not start yet";
    return folly::makeUnexpected(UnframedBodyOffsetTrackerError::NO_ERROR);
  }

  auto bodyStreamStart = ingressPrBodyTracker_.getBodyStreamStartOffset();
  if (bodyStreamStart.hasError()) {
    LOG(ERROR) << __func__ << ": error: " << bodyStreamStart.error();
    return folly::makeUnexpected(bodyStreamStart.error());
  }

  if (streamOffset < *bodyStreamStart) {
    LOG(ERROR) << __func__ << ": stream offset provided (" << streamOffset
               << ") is smaller than existing body stream offset: "
               << *bodyStreamStart;
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::INVALID_OFFSET);
  }

  auto bodyOffset = streamOffset - *bodyStreamStart;
  return bodyOffset;
}

folly::Expected<uint64_t, UnframedBodyOffsetTrackerError>
HQStreamCodec::onIngressDataExpired(uint64_t streamOffset) {
  CHECK(transportSupportsPartialReliability())
      << __func__
      << ": partially reliable operation on non-partially reliable transport";

  CHECK(ingressPartiallyReliable_)
      << __func__ << ": ingressPartiallyReliable_ is false";

  if (!finalIngressHeadersSeen_) {
    // If we haven't received headers fully yet, ignore dataExpired.
    // This is a legitimate situation that can happen with re-ordering/missing
    // data, but we're not dealing with it right now.
    // TODO: Cache the offset until later we receive the headers,
    //        and raise the callback then.
    VLOG(2)
        << __func__
        << ": received ingress dataExpired before HEADERS are fully received";
    return folly::makeUnexpected(UnframedBodyOffsetTrackerError::NO_ERROR);
  }

  auto bytesParsed = getCodecTotalBytesParsed();
  // Shouldn't happen, but still check.
  if (streamOffset <= bytesParsed) {
    LOG(ERROR) << "got stream offset (" << streamOffset
               << ") <= than bytes already processed (" << bytesParsed << ")";
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::INVALID_OFFSET);
  }

  CHECK(ingressPrBodyTracker_.bodyStarted())
      << ": partially realible body tracker not started";

  auto bodyStreamStart = ingressPrBodyTracker_.getBodyStreamStartOffset();
  if (bodyStreamStart.hasError()) {
    LOG(ERROR) << __func__ << ": error: " << bodyStreamStart.error();
    return folly::makeUnexpected(bodyStreamStart.error());
  }

  if (streamOffset <= *bodyStreamStart) {
    LOG(ERROR) << __func__ << ": stream offset provided (" << streamOffset
               << ") is smaller than existing body stream offset: "
               << *bodyStreamStart;
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::INVALID_OFFSET);
  }

  auto bodyOffset = streamOffset - *bodyStreamStart;
  ingressPrBodyTracker_.maybeMoveBodyBytesProcessed(bodyOffset);
  return bodyOffset;
}

folly::Expected<uint64_t, UnframedBodyOffsetTrackerError>
HQStreamCodec::onIngressDataRejected(uint64_t streamOffset) {
  CHECK(transportSupportsPartialReliability())
      << __func__
      << ": partially reliable operation on non-partially reliable transport";

  if (!finalEgressHeadersSeen_) {
    // If we haven't even sent the headers yet, ignore the dataReject.
    LOG(ERROR) << __func__
               << ": received DataRejected from receiver before egress headers "
                  "are sent";
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::INVALID_OFFSET);
  }

  auto bytesSent = getCodecTotalEgressBytes();
  if (!egressPrBodyTracker_.bodyStarted()) {
    egressPrBodyTracker_.startBodyTracking(bytesSent);
  }

  auto bodyStreamStart = egressPrBodyTracker_.getBodyStreamStartOffset();
  if (bodyStreamStart.hasError()) {
    LOG(ERROR) << __func__ << ": error: " << bodyStreamStart.error();
    return folly::makeUnexpected(bodyStreamStart.error());
  }

  if (streamOffset <= *bodyStreamStart) {
    LOG(ERROR) << __func__ << ": stream offset provided (" << streamOffset
               << ") is smaller than existing body stream offset: "
               << *bodyStreamStart;
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::INVALID_OFFSET);
  }

  auto bodyOffset = streamOffset - *bodyStreamStart;
  egressPrBodyTracker_.maybeMoveBodyBytesProcessed(bodyOffset);
  return bodyOffset;
}

folly::Expected<uint64_t, UnframedBodyOffsetTrackerError>
HQStreamCodec::onEgressBodySkip(uint64_t bodyOffset) {
  CHECK(transportSupportsPartialReliability())
      << __func__
      << ": partially reliable operation on non-partially reliable transport";

  if (!finalEgressHeadersSeen_) {
    // Application is trying to skip body data before having sent headers.
    LOG(ERROR) << __func__ << ": egress skip before HEADERS sent";
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::INVALID_OFFSET);
  }

  auto bytesSent = getCodecTotalEgressBytes();

  if (!egressPrBodyTracker_.bodyStarted()) {
    egressPrBodyTracker_.startBodyTracking(bytesSent);
  }

  egressPrBodyTracker_.maybeMoveBodyBytesProcessed(bodyOffset);
  auto streamOffset = egressPrBodyTracker_.appTostreamOffset(bodyOffset);
  if (streamOffset.hasError()) {
    LOG(ERROR) << __func__ << ": error: " << streamOffset.error();
    return folly::makeUnexpected(streamOffset.error());
  }
  return *streamOffset;
}

folly::Expected<uint64_t, UnframedBodyOffsetTrackerError>
HQStreamCodec::onEgressBodyReject(uint64_t bodyOffset) {
  CHECK(transportSupportsPartialReliability())
      << __func__
      << ": partially reliable operation on non-partially reliable transport";

  if (!finalIngressHeadersSeen_) {
    // Application is trying to skip body data before having received headers.
    LOG(ERROR) << __func__ << ": egress reject before HEADERS received";
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::INVALID_OFFSET);
  }

  auto bytesSent = getCodecTotalEgressBytes();
  if (!ingressPrBodyTracker_.bodyStarted()) {
    ingressPrBodyTracker_.startBodyTracking(bytesSent);
  }

  ingressPrBodyTracker_.maybeMoveBodyBytesProcessed(bodyOffset);
  auto streamOffset = ingressPrBodyTracker_.appTostreamOffset(bodyOffset);
  if (streamOffset.hasError()) {
    LOG(ERROR) << __func__ << ": error: " << streamOffset.error();
    return folly::makeUnexpected(streamOffset.error());
  }
  return *streamOffset;
}

void HQStreamCodec::onHeadersComplete(HTTPHeaderSize decodedSize,
                                      bool acknowledge) {
  decodeInfo_.onHeadersComplete(decodedSize);
  auto g = folly::makeGuard([this] { setParserPaused(false); });
  auto g2 = folly::makeGuard(activationHook_());
  std::unique_ptr<HTTPMessage> msg = std::move(decodeInfo_.msg);

  // Check parsing error
  if (decodeInfo_.parsingError != "") {
    LOG(ERROR) << "Failed parsing header list for stream=" << streamId_
               << ", error=" << decodeInfo_.parsingError;
    HTTPException err(
        HTTPException::Direction::INGRESS,
        folly::format(
            "HQStreamCodec stream error: stream={} status={} error:{}",
            streamId_,
            400,
            decodeInfo_.parsingError)
            .str());
    err.setHttpStatusCode(400);
    callback_->onError(streamId_, err, true);
    return;
  }
  msg->setAdvancedProtocolString(getCodecProtocolString(CodecProtocol::HQ));

  if (curHeader_.type == hq::FrameType::HEADERS) {
    if (!finalIngressHeadersSeen_ &&
        (msg->isRequest() || !msg->is1xxResponse())) {
      finalIngressHeadersSeen_ = true;
    }
  }

  if (acknowledge) {
    qpackDecoderWriteBuf_.append(headerCodec_.encodeHeaderAck(streamId_));
  }
  // Report back what we've parsed
  if (callback_) {
    // TODO: should we treat msg as chunked like H2?
    callback_->onHeadersComplete(streamId_, std::move(msg));
  }
}

void HQStreamCodec::onDecodeError(HPACK::DecodeError decodeError) {
  // the parser may be paused, but this codec is dead.
  decodeInfo_.decodeError = decodeError;
  DCHECK_NE(decodeInfo_.decodeError, HPACK::DecodeError::NONE);
  LOG(ERROR) << "Failed decoding header block for stream=" << streamId_;

  if (decodeInfo_.msg) {
    // print the partial message
    decodeInfo_.msg->dumpMessage(3);
  }

  if (callback_) {
    auto g = folly::makeGuard(activationHook_());
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                     "Stream headers decompression error");
    ex.setErrno(uint32_t(HTTP3::ErrorCode::HTTP_QPACK_DECOMPRESSION_FAILED));
    callback_->onError(kSessionStreamId, ex, false);
  }
  decodeInfo_.msg.reset();
}

void HQStreamCodec::generateHeader(folly::IOBufQueue& writeBuf,
                                   StreamID stream,
                                   const HTTPMessage& msg,
                                   bool /*eom*/,
                                   HTTPHeaderSize* size) {
  DCHECK_EQ(stream, streamId_);

  // Partial reliability might already be set by ingress.
  egressPartiallyReliable_ =
      egressPartiallyReliable_ || msg.isPartiallyReliable();

  generateHeaderImpl(writeBuf, msg, folly::none, size);

  // For requests, set final header seen flag right away.
  // For responses, header is final only if response code is >= 200.
  if (msg.isRequest() || (msg.isResponse() && msg.getStatusCode() >= 200)) {
    finalEgressHeadersSeen_ = true;
  }

  if (egressPartiallyReliable_ && finalEgressHeadersSeen_) {
    // Piggyback a 0-length DATA frame to kick off partially reliable body
    // streaming.
    CHECK(!egressPrBodyTracker_.bodyStarted())
        << ": egress partially relialble body tracker already started";
    auto curBytesSent = getCodecTotalEgressBytes();
    auto res = hq::writeFrameHeader(writeBuf, FrameType::DATA, 0);
    if (res.hasValue()) {
      totalEgressBytes_ += res.value();
      auto bodyStreamOffset = curBytesSent + *res;
      egressPrBodyTracker_.startBodyTracking(bodyStreamOffset);
    } else {
      LOG(ERROR) << __func__
                 << ": failed to write 0-length DATA frame: " << res.error();
    }
  }
}

void HQStreamCodec::generatePushPromise(folly::IOBufQueue& writeBuf,
                                        StreamID stream,
                                        const HTTPMessage& msg,
                                        StreamID pushId,
                                        bool /*eom*/,
                                        HTTPHeaderSize* size) {
  DCHECK_EQ(stream, streamId_);
  DCHECK(transportDirection_ == TransportDirection::DOWNSTREAM);
  CHECK(!egressPartiallyReliable_)
      << __func__ << ": not allowed in partially reliable mode";
  generateHeaderImpl(writeBuf, msg, pushId, size);
}

void HQStreamCodec::generateHeaderImpl(folly::IOBufQueue& writeBuf,
                                       const HTTPMessage& msg,
                                       folly::Optional<StreamID> pushId,
                                       HTTPHeaderSize* size) {
  auto result = headerCodec_.encodeHTTP(
      qpackEncoderWriteBuf_, msg, true, streamId_, maxEncoderStreamData());
  if (size) {
    *size = headerCodec_.getEncodedSize();
  }

  if (headerCodec_.getEncodedSize().uncompressed >
      ingressSettings_.getSetting(SettingsId::MAX_HEADER_LIST_SIZE,
                                  std::numeric_limits<uint32_t>::max())) {
    // The remote side told us they don't want headers this large...
    // but this function has no mechanism to fail
    string serializedHeaders;
    msg.getHeaders().forEach(
        [&serializedHeaders](const string& name, const string& value) {
          serializedHeaders =
              folly::to<string>(serializedHeaders, "\\n", name, ":", value);
        });
    LOG(ERROR) << "generating HEADERS frame larger than peer maximum nHeaders="
               << msg.getHeaders().size()
               << " all headers=" << serializedHeaders;
  }

  // HTTP2/2 serializes priority here, but HQ priorities need to go on the
  // control stream

  WriteResult res;
  if (pushId) {
    CHECK(!egressPartiallyReliable_)
        << ": push promise not allowed on partially reliable message";
    res = hq::writePushPromise(writeBuf, *pushId, std::move(result));
  } else {
    res = hq::writeHeaders(writeBuf, std::move(result));
  }

  if (res.hasValue()) {
    totalEgressBytes_ += res.value();
  } else {
    LOG(ERROR) << __func__ << ": failed to write "
               << ((pushId) ? "push promise: " : "headers: ") << res.error();
  }
}

size_t HQStreamCodec::generateBodyImpl(folly::IOBufQueue& writeBuf,
                                       std::unique_ptr<folly::IOBuf> chain) {
  auto result = hq::writeData(writeBuf, std::move(chain));
  if (result) {
    return *result;
  }
  LOG(FATAL) << "frame exceeded 2^62-1 limit";
  return 0;
}

size_t HQStreamCodec::generatePartiallyReliableBodyImpl(
    folly::IOBufQueue& writeBuf, std::unique_ptr<folly::IOBuf> chain) {
  CHECK(transportSupportsPartialReliability())
      << __func__
      << ": partially reliable operation on non-partially reliable transport";

  auto bodyLen = chain->computeChainDataLength();

  CHECK(egressPrBodyTracker_.bodyStarted())
      << ": partially realible body tracker not started";
  auto result = hq::writeUnframedBytes(writeBuf, std::move(chain));

  if (result) {
    egressPrBodyTracker_.addBodyBytesProcessed(bodyLen);
    return *result;
  }
  CHECK(false) << "failed to write unframed data";
  return 0;
}

size_t HQStreamCodec::generateBody(folly::IOBufQueue& writeBuf,
                                   StreamID stream,
                                   std::unique_ptr<folly::IOBuf> chain,
                                   folly::Optional<uint8_t> /*padding*/,
                                   bool /*eom*/) {
  DCHECK_EQ(stream, streamId_);

  size_t bytesWritten = 0;
  if (egressPartiallyReliable_) {
    bytesWritten =
        generatePartiallyReliableBodyImpl(writeBuf, std::move(chain));
  } else {
    bytesWritten = generateBodyImpl(writeBuf, std::move(chain));
  }

  totalEgressBytes_ += bytesWritten;
  return bytesWritten;
}

size_t HQStreamCodec::generateEOM(folly::IOBufQueue& /*writeBuf*/,
                                  StreamID stream) {
  // Generate EOM is a no-op
  DCHECK_EQ(stream, streamId_);
  return 0;
}

CompressionInfo HQStreamCodec::getCompressionInfo() const {
  return headerCodec_.getCompressionInfo();
}

}} // namespace proxygen::hq
