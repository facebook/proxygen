/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/HTTP2Codec.h>
#include <proxygen/lib/http/codec/HTTP2Constants.h>
#include <proxygen/lib/http/codec/CodecUtil.h>
#include <proxygen/lib/utils/Logging.h>
#include <proxygen/lib/utils/Base64.h>

#include <folly/Conv.h>
#include <folly/Random.h>
#include <folly/ThreadLocal.h>
#include <folly/io/Cursor.h>
#include <folly/tracing/ScopedTraceSection.h>
#include <type_traits>

using namespace proxygen::compress;
using namespace folly::io;
using namespace folly;

using std::string;

namespace {
std::string base64url_encode(ByteRange range) {
  return proxygen::Base64::urlEncode(range);
}

std::string base64url_decode(const std::string& str) {
  return proxygen::Base64::urlDecode(str);
}

}

namespace proxygen {


HTTP2Codec::HTTP2Codec(TransportDirection direction)
    : HTTPParallelCodec(direction),
      headerCodec_(direction),
      frameState_(direction == TransportDirection::DOWNSTREAM
                  ? FrameState::UPSTREAM_CONNECTION_PREFACE
                  : FrameState::DOWNSTREAM_CONNECTION_PREFACE) {

  const auto maxHeaderListSize = egressSettings_.getSetting(
    SettingsId::MAX_HEADER_LIST_SIZE);
  if (maxHeaderListSize) {
    headerCodec_.setMaxUncompressed(maxHeaderListSize->value);
  }

  VLOG(4) << "creating " << getTransportDirectionString(direction)
          << " HTTP/2 codec";
}

HTTP2Codec::~HTTP2Codec() {}

// HTTPCodec API

size_t HTTP2Codec::onIngress(const folly::IOBuf& buf) {
  // TODO: ensure only 1 parse at a time on stack.
  FOLLY_SCOPED_TRACE_SECTION("HTTP2Codec - onIngress");

  Cursor cursor(&buf);
  size_t parsed = 0;
  ErrorCode connError = ErrorCode::NO_ERROR;
  for (auto bufLen = cursor.totalLength();
       connError == ErrorCode::NO_ERROR;
       bufLen = cursor.totalLength()) {
    if (frameState_ == FrameState::UPSTREAM_CONNECTION_PREFACE) {
      if (bufLen >= http2::kConnectionPreface.length()) {
        auto test = cursor.readFixedString(http2::kConnectionPreface.length());
        parsed += http2::kConnectionPreface.length();
        if (test != http2::kConnectionPreface) {
          goawayErrorMessage_ = "missing connection preface";
          VLOG(4) << goawayErrorMessage_;
          connError = ErrorCode::PROTOCOL_ERROR;
        }
        frameState_ = FrameState::FRAME_HEADER;
      } else {
        break;
      }
    } else if (frameState_ == FrameState::FRAME_HEADER ||
               frameState_ == FrameState::DOWNSTREAM_CONNECTION_PREFACE) {
      // Waiting to parse the common frame header
      if (bufLen >= http2::kFrameHeaderSize) {
        connError = parseFrameHeader(cursor, curHeader_);
        parsed += http2::kFrameHeaderSize;
        if (frameState_ == FrameState::DOWNSTREAM_CONNECTION_PREFACE &&
            curHeader_.type != http2::FrameType::SETTINGS) {
          goawayErrorMessage_ = folly::to<string>(
              "GOAWAY error: got invalid connection preface frame type=",
              getFrameTypeString(curHeader_.type), "(", curHeader_.type, ")",
              " for streamID=", curHeader_.stream);
          VLOG(4) << goawayErrorMessage_;
          connError = ErrorCode::PROTOCOL_ERROR;
        }
        if (curHeader_.length > maxRecvFrameSize()) {
          VLOG(4) << "Excessively large frame len=" << curHeader_.length;
          connError = ErrorCode::FRAME_SIZE_ERROR;
        }

        if (callback_) {
          callback_->onFrameHeader(
            curHeader_.stream,
            curHeader_.flags,
            curHeader_.length,
            static_cast<uint8_t>(curHeader_.type));
        }

        frameState_ = (curHeader_.type == http2::FrameType::DATA) ?
          FrameState::DATA_FRAME_DATA : FrameState::FRAME_DATA;
        pendingDataFrameBytes_ = curHeader_.length;
        pendingDataFramePaddingBytes_ = 0;
#ifndef NDEBUG
        receivedFrameCount_++;
#endif
      } else {
        break;
      }
    } else if (frameState_ == FrameState::DATA_FRAME_DATA && bufLen > 0 &&
               (bufLen < curHeader_.length ||
                pendingDataFrameBytes_ < curHeader_.length)) {
      // FrameState::DATA_FRAME_DATA with partial data only
      size_t dataParsed = 0;
      connError = parseDataFrameData(cursor, bufLen, dataParsed);
      if (dataParsed == 0 && pendingDataFrameBytes_ > 0) {
        // We received only the padding byte, we will wait for more
        break;
      } else {
        parsed += dataParsed;
        if (pendingDataFrameBytes_ == 0) {
          frameState_ = FrameState::FRAME_HEADER;
        }
      }
    } else { // FrameState::FRAME_DATA
             // or FrameState::DATA_FRAME_DATA with all data available
      // Already parsed the common frame header
      const auto frameLen = curHeader_.length;
      if (bufLen >= frameLen) {
        connError = parseFrame(cursor);
        parsed += curHeader_.length;
        frameState_ = FrameState::FRAME_HEADER;
      } else {
        break;
      }
    }
  }
  checkConnectionError(connError, &buf);
  return parsed;
}

ErrorCode HTTP2Codec::parseFrame(folly::io::Cursor& cursor) {
  FOLLY_SCOPED_TRACE_SECTION("HTTP2Codec - parseFrame");
  if (expectedContinuationStream_ != 0 &&
       (curHeader_.type != http2::FrameType::CONTINUATION ||
        expectedContinuationStream_ != curHeader_.stream)) {
    goawayErrorMessage_ = folly::to<string>(
        "GOAWAY error: while expected CONTINUATION with stream=",
        expectedContinuationStream_, ", received streamID=", curHeader_.stream,
        " of type=", getFrameTypeString(curHeader_.type));
    VLOG(4) << goawayErrorMessage_;
    return ErrorCode::PROTOCOL_ERROR;
  }
  if (expectedContinuationStream_ == 0 &&
      curHeader_.type == http2::FrameType::CONTINUATION) {
    goawayErrorMessage_ = folly::to<string>(
        "GOAWAY error: unexpected CONTINUATION received with streamID=",
        curHeader_.stream);
    VLOG(4) << goawayErrorMessage_;
    return ErrorCode::PROTOCOL_ERROR;
  }
  if (frameAffectsCompression(curHeader_.type) &&
      curHeaderBlock_.chainLength() + curHeader_.length >
      egressSettings_.getSetting(SettingsId::MAX_HEADER_LIST_SIZE, 0)) {
    // this may be off by up to the padding length (max 255), but
    // these numbers are already so generous, and we're comparing the
    // max-uncompressed to the actual compressed size.  Let's fail
    // before buffering.

    // TODO(t6513634): it would be nicer to stream-process this header
    // block to keep the connection state consistent without consuming
    // memory, and fail just the request per the HTTP/2 spec (section
    // 10.3)
    goawayErrorMessage_ = folly::to<string>(
      "Failing connection due to excessively large headers");
    LOG(ERROR) << goawayErrorMessage_;
    return ErrorCode::PROTOCOL_ERROR;
  }

  expectedContinuationStream_ =
    (frameAffectsCompression(curHeader_.type) &&
     !(curHeader_.flags & http2::END_HEADERS)) ? curHeader_.stream : 0;

  switch (curHeader_.type) {
    case http2::FrameType::DATA:
      return parseAllData(cursor);
    case http2::FrameType::HEADERS:
      return parseHeaders(cursor);
    case http2::FrameType::PRIORITY:
      return parsePriority(cursor);
    case http2::FrameType::RST_STREAM:
      return parseRstStream(cursor);
    case http2::FrameType::SETTINGS:
      return parseSettings(cursor);
    case http2::FrameType::PUSH_PROMISE:
      return parsePushPromise(cursor);
    case http2::FrameType::EX_HEADERS:
      if (ingressSettings_.getSetting(SettingsId::ENABLE_EX_HEADERS, 0)) {
        return parseExHeaders(cursor);
      } else {
        VLOG(2) << "EX_HEADERS not enabled, ignoring the frame";
        break;
      }
    case http2::FrameType::PING:
      return parsePing(cursor);
    case http2::FrameType::GOAWAY:
      return parseGoaway(cursor);
    case http2::FrameType::WINDOW_UPDATE:
      return parseWindowUpdate(cursor);
    case http2::FrameType::CONTINUATION:
      return parseContinuation(cursor);
    case http2::FrameType::ALTSVC:
      // fall through, unimplemented
      break;
    case http2::FrameType::CERTIFICATE_REQUEST:
      return parseCertificateRequest(cursor);
    case http2::FrameType::CERTIFICATE:
      return parseCertificate(cursor);
    default:
      // Implementations MUST ignore and discard any frame that has a
      // type that is unknown
      break;
  }

  // Landing here means unknown, unimplemented or ignored frame.
  VLOG(2) << "Skipping frame (type=" << (uint8_t)curHeader_.type << ")";
  cursor.skip(curHeader_.length);
  return ErrorCode::NO_ERROR;
}

ErrorCode HTTP2Codec::handleEndStream() {
  if (curHeader_.type != http2::FrameType::HEADERS &&
      curHeader_.type != http2::FrameType::EX_HEADERS &&
      curHeader_.type != http2::FrameType::CONTINUATION &&
      curHeader_.type != http2::FrameType::DATA) {
    return ErrorCode::NO_ERROR;
  }

  // do we need to handle case where this stream has already aborted via
  // another callback (onHeadersComplete/onBody)?
  pendingEndStreamHandling_ |= (curHeader_.flags & http2::END_STREAM);

  // with a websocket upgrade, we need to send message complete cb to
  // mirror h1x codec's behavior. when the stream closes, we need to
  // send another callback to clean up the stream's resources.
  if (ingressWebsocketUpgrade_) {
    ingressWebsocketUpgrade_ = false;
    deliverCallbackIfAllowed(&HTTPCodec::Callback::onMessageComplete,
                             "onMessageComplete", curHeader_.stream, true);
  }

  if (pendingEndStreamHandling_ && expectedContinuationStream_ == 0) {
    pendingEndStreamHandling_ = false;
    deliverCallbackIfAllowed(&HTTPCodec::Callback::onMessageComplete,
                             "onMessageComplete", curHeader_.stream, false);
  }
  return ErrorCode::NO_ERROR;
}

ErrorCode HTTP2Codec::parseAllData(Cursor& cursor) {
  std::unique_ptr<IOBuf> outData;
  uint16_t padding = 0;
  VLOG(10) << "parsing all frame DATA bytes for stream=" << curHeader_.stream <<
    " length=" << curHeader_.length;
  auto ret = http2::parseData(cursor, curHeader_, outData, padding);
  RETURN_IF_ERROR(ret);

  if (callback_ && (padding > 0 || (outData && !outData->empty()))) {
    if (!outData) {
      outData = std::make_unique<IOBuf>();
    }
    deliverCallbackIfAllowed(&HTTPCodec::Callback::onBody, "onBody",
                             curHeader_.stream, std::move(outData), padding);
  }
  return handleEndStream();
}

ErrorCode HTTP2Codec::parseDataFrameData(Cursor& cursor,
                                         size_t bufLen,
                                         size_t& parsed) {
  FOLLY_SCOPED_TRACE_SECTION("HTTP2Codec - parseDataFrameData");
  if (bufLen == 0) {
    VLOG(10) << "No data to parse";
    return ErrorCode::NO_ERROR;
  }

  std::unique_ptr<IOBuf> outData;
  uint16_t padding = 0;
  VLOG(10) << "parsing DATA frame data for stream=" << curHeader_.stream <<
    " frame data length=" << curHeader_.length << " pendingDataFrameBytes_=" <<
    pendingDataFrameBytes_ << " pendingDataFramePaddingBytes_=" <<
    pendingDataFramePaddingBytes_ << " bufLen=" << bufLen <<
    " parsed=" << parsed;
  // Parse the padding information only the first time
  if (pendingDataFrameBytes_ == curHeader_.length &&
    pendingDataFramePaddingBytes_ == 0) {
    if (frameHasPadding(curHeader_) && bufLen == 1) {
      // We need to wait for more bytes otherwise we won't be able to pass
      // the correct padding to the first onBody call
      return ErrorCode::NO_ERROR;
    }
    const auto ret = http2::parseDataBegin(cursor, curHeader_, parsed, padding);
    RETURN_IF_ERROR(ret);
    if (padding > 0) {
      pendingDataFramePaddingBytes_ = padding - 1;
      pendingDataFrameBytes_--;
      bufLen--;
      parsed++;
    }
    VLOG(10) << "out padding=" << padding << " pendingDataFrameBytes_=" <<
      pendingDataFrameBytes_ << " pendingDataFramePaddingBytes_=" <<
      pendingDataFramePaddingBytes_ << " bufLen=" << bufLen <<
      " parsed=" << parsed;
  }
  if (bufLen > 0) {
    // Check if we have application data to parse
    if (pendingDataFrameBytes_ > pendingDataFramePaddingBytes_) {
      const size_t pendingAppData =
        pendingDataFrameBytes_ - pendingDataFramePaddingBytes_;
      const size_t toClone = std::min(pendingAppData, bufLen);
      cursor.clone(outData, toClone);
      bufLen -= toClone;
      pendingDataFrameBytes_ -= toClone;
      parsed += toClone;
      VLOG(10) << "parsed some app data, pendingDataFrameBytes_=" <<
        pendingDataFrameBytes_ << " pendingDataFramePaddingBytes_=" <<
        pendingDataFramePaddingBytes_ << " bufLen=" << bufLen <<
        " parsed=" << parsed;
    }
    // Check if we have padding bytes to parse
    if (bufLen > 0 && pendingDataFramePaddingBytes_ > 0) {
      size_t toSkip = 0;
      auto ret = http2::parseDataEnd(cursor, bufLen,
                                     pendingDataFramePaddingBytes_, toSkip);
      RETURN_IF_ERROR(ret);
      pendingDataFrameBytes_ -= toSkip;
      pendingDataFramePaddingBytes_ -= toSkip;
      parsed += toSkip;
      VLOG(10) << "parsed some padding, pendingDataFrameBytes_=" <<
        pendingDataFrameBytes_ << " pendingDataFramePaddingBytes_=" <<
        pendingDataFramePaddingBytes_ << " bufLen=" << bufLen <<
        " parsed=" << parsed;
    }
  }

  if (callback_ && (padding > 0 || (outData && !outData->empty()))) {
    if (!outData) {
      outData = std::make_unique<IOBuf>();
    }
    deliverCallbackIfAllowed(&HTTPCodec::Callback::onBody, "onBody",
                             curHeader_.stream, std::move(outData), padding);
  }
  return (pendingDataFrameBytes_ > 0) ? ErrorCode::NO_ERROR : handleEndStream();
}


ErrorCode HTTP2Codec::parseHeaders(Cursor& cursor) {
  FOLLY_SCOPED_TRACE_SECTION("HTTP2Codec - parseHeaders");
  folly::Optional<http2::PriorityUpdate> priority;
  std::unique_ptr<IOBuf> headerBuf;
  VLOG(4) << "parsing HEADERS frame for stream=" << curHeader_.stream <<
    " length=" << curHeader_.length;
  auto err = http2::parseHeaders(cursor, curHeader_, priority, headerBuf);
  RETURN_IF_ERROR(err);
  if (transportDirection_ == TransportDirection::DOWNSTREAM) {
    RETURN_IF_ERROR(
        checkNewStream(curHeader_.stream, true /* trailersAllowed */));
  }
  err = parseHeadersImpl(cursor, std::move(headerBuf), priority, folly::none,
                         folly::none);
  return err;
}

ErrorCode HTTP2Codec::parseExHeaders(Cursor& cursor) {
  FOLLY_SCOPED_TRACE_SECTION("HTTP2Codec - parseExHeaders");
  HTTPCodec::ExAttributes exAttributes;
  folly::Optional<http2::PriorityUpdate> priority;
  std::unique_ptr<IOBuf> headerBuf;
  VLOG(4) << "parsing ExHEADERS frame for stream=" << curHeader_.stream
          << " length=" << curHeader_.length;
  auto err = http2::parseExHeaders(
      cursor, curHeader_, exAttributes, priority, headerBuf);
  RETURN_IF_ERROR(err);
  if (isRequest(curHeader_.stream)) {
    RETURN_IF_ERROR(
        checkNewStream(curHeader_.stream, false /* trailersAllowed */));
  }
  return parseHeadersImpl(cursor, std::move(headerBuf), priority, folly::none,
                          exAttributes);
}

ErrorCode HTTP2Codec::parseContinuation(Cursor& cursor) {
  std::unique_ptr<IOBuf> headerBuf;
  VLOG(4) << "parsing CONTINUATION frame for stream=" << curHeader_.stream <<
    " length=" << curHeader_.length;
  auto err = http2::parseContinuation(cursor, curHeader_, headerBuf);
  RETURN_IF_ERROR(err);
  err = parseHeadersImpl(cursor, std::move(headerBuf),
                         folly::none, folly::none, folly::none);
  return err;
}

ErrorCode HTTP2Codec::parseHeadersImpl(
    Cursor& /*cursor*/,
    std::unique_ptr<IOBuf> headerBuf,
    const folly::Optional<http2::PriorityUpdate>& priority,
    const folly::Optional<uint32_t>& promisedStream,
    const folly::Optional<ExAttributes>& exAttributes) {
  curHeaderBlock_.append(std::move(headerBuf));
  std::unique_ptr<HTTPMessage> msg;
  if (curHeader_.flags & http2::END_HEADERS) {
    auto errorCode =
        parseHeadersDecodeFrames(priority, promisedStream, exAttributes, msg);
    if (errorCode.hasValue()) {
      return errorCode.value();
    }
  }

  // if we're not parsing CONTINUATION, then it's start of new header block
  if (curHeader_.type != http2::FrameType::CONTINUATION) {
    headerBlockFrameType_ = curHeader_.type;
  }

  // Report back what we've parsed
  if (callback_) {
    auto concurError = parseHeadersCheckConcurrentStreams(priority);
    if (concurError.hasValue()) {
      return concurError.value();
    }
    uint32_t headersCompleteStream = curHeader_.stream;
    bool trailers = parsingTrailers();
    bool allHeaderFramesReceived =
        (curHeader_.flags & http2::END_HEADERS) &&
        (headerBlockFrameType_ == http2::FrameType::HEADERS);
    if (allHeaderFramesReceived && !trailers) {
      // Only deliver onMessageBegin once per stream.
      // For responses with CONTINUATION, this will be delayed until
      // the frame with the END_HEADERS flag set.
      if (!deliverCallbackIfAllowed(&HTTPCodec::Callback::onMessageBegin,
                                    "onMessageBegin",
                                    curHeader_.stream,
                                    msg.get())) {
        return handleEndStream();
      }
   } else if (curHeader_.type == http2::FrameType::EX_HEADERS) {
      if (!deliverCallbackIfAllowed(&HTTPCodec::Callback::onExMessageBegin,
                                    "onExMessageBegin",
                                    curHeader_.stream,
                                    exAttributes->controlStream,
                                    exAttributes->unidirectional,
                                    msg.get())) {
        return handleEndStream();
      }
    } else if (curHeader_.type == http2::FrameType::PUSH_PROMISE) {
      DCHECK(promisedStream);
      if (!deliverCallbackIfAllowed(&HTTPCodec::Callback::onPushMessageBegin,
                                    "onPushMessageBegin", *promisedStream,
                                    curHeader_.stream, msg.get())) {
        return handleEndStream();
      }
      headersCompleteStream = *promisedStream;
    }

    if (curHeader_.flags & http2::END_HEADERS && msg) {
      if (!(curHeader_.flags & http2::END_STREAM)) {
        // If it there are DATA frames coming, consider it chunked
        msg->setIsChunked(true);
      }
      if (trailers) {
        VLOG(4) << "Trailers complete for streamId=" << headersCompleteStream
                << " direction=" << transportDirection_;
        auto trailerHeaders =
            std::make_unique<HTTPHeaders>(msg->extractHeaders());
        msg.reset();
        callback_->onTrailersComplete(headersCompleteStream,
                                      std::move(trailerHeaders));
      } else {
        callback_->onHeadersComplete(headersCompleteStream, std::move(msg));
      }
    }
    return handleEndStream();
  }
  return ErrorCode::NO_ERROR;
}

folly::Optional<ErrorCode> HTTP2Codec::parseHeadersDecodeFrames(
    const folly::Optional<http2::PriorityUpdate>& priority,
    const folly::Optional<uint32_t>& promisedStream,
    const folly::Optional<ExAttributes>& exAttributes,
    std::unique_ptr<HTTPMessage>& msg) {
  // decompress headers
  Cursor headerCursor(curHeaderBlock_.front());
  bool isReq = false;
  if (promisedStream) {
    isReq = true;
  } else if (exAttributes) {
    isReq = isRequest(curHeader_.stream);
  } else {
    isReq = transportDirection_ == TransportDirection::DOWNSTREAM;
  }

  // Validate circular dependencies.
  if (priority && (curHeader_.stream == priority->streamDependency)) {
    streamError(
        folly::to<string>("Circular dependency for txn=", curHeader_.stream),
        ErrorCode::PROTOCOL_ERROR,
        curHeader_.type == http2::FrameType::HEADERS);
    return ErrorCode::NO_ERROR;
  }

  decodeInfo_.init(isReq, parsingDownstreamTrailers_);
  if (priority) {
    decodeInfo_.msg->setHTTP2Priority(
        std::make_tuple(priority->streamDependency,
                        priority->exclusive,
                        priority->weight));
  }

  headerCodec_.decodeStreaming(
      headerCursor, curHeaderBlock_.chainLength(), this);
  msg = std::move(decodeInfo_.msg);
  // Saving this in case we need to log it on error
  auto g = folly::makeGuard([this] { curHeaderBlock_.move(); });
  // Check decoding error
  if (decodeInfo_.decodeError != HPACK::DecodeError::NONE) {
    static const std::string decodeErrorMessage =
        "Failed decoding header block for stream=";
    // Avoid logging header blocks that have failed decoding due to being
    // excessively large.
    if (decodeInfo_.decodeError != HPACK::DecodeError::HEADERS_TOO_LARGE) {
      LOG(ERROR) << decodeErrorMessage << curHeader_.stream
                 << " header block=";
      VLOG(3) << IOBufPrinter::printHexFolly(curHeaderBlock_.front(), true);
    } else {
      LOG(ERROR) << decodeErrorMessage << curHeader_.stream;
    }

    if (msg) {
      // print the partial message
      msg->dumpMessage(3);
    }
    return ErrorCode::COMPRESSION_ERROR;
  }

  // Check parsing error
  if (decodeInfo_.parsingError != "") {
    LOG(ERROR) << "Failed parsing header list for stream=" << curHeader_.stream
               << ", error=" << decodeInfo_.parsingError << ", header block=";
    VLOG(3) << IOBufPrinter::printHexFolly(curHeaderBlock_.front(), true);
    HTTPException err(HTTPException::Direction::INGRESS,
                      folly::to<std::string>("HTTP2Codec stream error: ",
                                             "stream=",
                                             curHeader_.stream,
                                             " status=",
                                             400,
                                             " error: ",
                                             decodeInfo_.parsingError));
    err.setHttpStatusCode(400);
    callback_->onError(curHeader_.stream, err, true);
    return ErrorCode::NO_ERROR;
  }

  return folly::Optional<ErrorCode>();
}

folly::Optional<ErrorCode> HTTP2Codec::parseHeadersCheckConcurrentStreams(
    const folly::Optional<http2::PriorityUpdate>& priority) {
  if (curHeader_.type == http2::FrameType::HEADERS ||
      curHeader_.type == http2::FrameType::EX_HEADERS) {
    if (curHeader_.flags & http2::PRIORITY) {
      DCHECK(priority);
      // callback_->onPriority(priority.get());
    }

    // callback checks total number of streams is smaller than settings max
    if (callback_->numIncomingStreams() >=
        egressSettings_.getSetting(SettingsId::MAX_CONCURRENT_STREAMS,
                                   std::numeric_limits<int32_t>::max())) {
      streamError(folly::to<string>("Exceeded max_concurrent_streams"),
                  ErrorCode::REFUSED_STREAM, true);
      return ErrorCode::NO_ERROR;
    }
  }
  return folly::Optional<ErrorCode>();
}

void HTTP2Codec::onHeader(const folly::fbstring& name,
                          const folly::fbstring& value) {
  if (decodeInfo_.onHeader(name, value)) {
    if (name == "user-agent" && userAgent_.empty()) {
      userAgent_ = value.toStdString();
    }
  } else {
    VLOG(4) << "dir=" << uint32_t(transportDirection_) <<
      decodeInfo_.parsingError << " codec=" << headerCodec_;
  }
}

void HTTP2Codec::onHeadersComplete(HTTPHeaderSize decodedSize,
                                   bool /*acknowledge*/) {
  decodeInfo_.onHeadersComplete(decodedSize);
  decodeInfo_.msg->setAdvancedProtocolString(http2::kProtocolString);

  HTTPMessage* msg = decodeInfo_.msg.get();
  HTTPRequestVerifier& verifier = decodeInfo_.verifier;
  if ((transportDirection_ == TransportDirection::DOWNSTREAM) &&
      verifier.hasUpgradeProtocol() &&
      (*msg->getUpgradeProtocol() == headers::kWebsocketString) &&
      msg->getMethod() == HTTPMethod::CONNECT) {
    msg->setIngressWebsocketUpgrade();
    ingressWebsocketUpgrade_ = true;
  } else {
    auto it = upgradedStreams_.find(curHeader_.stream);
    if (it != upgradedStreams_.end()) {
      upgradedStreams_.erase(curHeader_.stream);
      // a websocket upgrade was sent on this stream.
      if (msg->getStatusCode() != 200) {
        decodeInfo_.parsingError =
          folly::to<string>("Invalid response code to a websocket upgrade: ",
                            msg->getStatusCode());
        return;
      }
      msg->setIngressWebsocketUpgrade();
    }
  }
}

void HTTP2Codec::onDecodeError(HPACK::DecodeError decodeError) {
  decodeInfo_.decodeError = decodeError;
}

ErrorCode HTTP2Codec::parsePriority(Cursor& cursor) {
  VLOG(4) << "parsing PRIORITY frame for stream=" << curHeader_.stream <<
    " length=" << curHeader_.length;
  http2::PriorityUpdate pri;
  auto err = http2::parsePriority(cursor, curHeader_, pri);
  RETURN_IF_ERROR(err);
  if (curHeader_.stream == pri.streamDependency) {
    streamError(folly::to<string>("Circular dependency for txn=",
                                  curHeader_.stream),
                ErrorCode::PROTOCOL_ERROR, false);
    return ErrorCode::NO_ERROR;
  }
  deliverCallbackIfAllowed(&HTTPCodec::Callback::onPriority, "onPriority",
                           curHeader_.stream,
                           std::make_tuple(pri.streamDependency,
                                           pri.exclusive,
                                           pri.weight));
  return ErrorCode::NO_ERROR;
}

size_t HTTP2Codec::addPriorityNodes(
    PriorityQueue& queue,
    folly::IOBufQueue& writeBuf,
    uint8_t maxLevel) {
  HTTPCodec::StreamID parent = 0;
  size_t bytes = 0;
  while (maxLevel--) {
    auto id = createStream();
    virtualPriorityNodes_.push_back(id);
    queue.addPriorityNode(id, parent);
    bytes += generatePriority(writeBuf, id, std::make_tuple(parent, false, 0));
    parent = id;
  }
  return bytes;
}

ErrorCode HTTP2Codec::parseRstStream(Cursor& cursor) {
  // rst for stream in idle state - protocol error
  VLOG(4) << "parsing RST_STREAM frame for stream=" << curHeader_.stream <<
    " length=" << curHeader_.length;
  upgradedStreams_.erase(curHeader_.stream);
  ErrorCode statusCode = ErrorCode::NO_ERROR;
  auto err = http2::parseRstStream(cursor, curHeader_, statusCode);
  RETURN_IF_ERROR(err);
  if (statusCode == ErrorCode::PROTOCOL_ERROR) {
    goawayErrorMessage_ = folly::to<string>(
        "GOAWAY error: RST_STREAM with code=", getErrorCodeString(statusCode),
        " for streamID=", curHeader_.stream, " user-agent=", userAgent_);
    VLOG(2) << goawayErrorMessage_;
  }
  deliverCallbackIfAllowed(&HTTPCodec::Callback::onAbort, "onAbort",
                           curHeader_.stream, statusCode);
  return ErrorCode::NO_ERROR;
}

ErrorCode HTTP2Codec::parseSettings(Cursor& cursor) {
  VLOG(4) << "parsing SETTINGS frame for stream=" << curHeader_.stream <<
    " length=" << curHeader_.length;
  std::deque<SettingPair> settings;
  auto err = http2::parseSettings(cursor, curHeader_, settings);
  RETURN_IF_ERROR(err);
  if (curHeader_.flags & http2::ACK) {
    handleSettingsAck();
    return ErrorCode::NO_ERROR;
  }
  return handleSettings(settings);
}

void HTTP2Codec::handleSettingsAck() {
  if (pendingTableMaxSize_) {
    headerCodec_.setDecoderHeaderTableMaxSize(*pendingTableMaxSize_);
    pendingTableMaxSize_ = folly::none;
  }
  if (callback_) {
    callback_->onSettingsAck();
  }
}

ErrorCode HTTP2Codec::handleSettings(const std::deque<SettingPair>& settings) {
  SettingsList settingsList;
  for (auto& setting: settings) {
    switch (setting.first) {
      case SettingsId::HEADER_TABLE_SIZE:
      {
        uint32_t tableSize = setting.second;
        if (setting.second > http2::kMaxHeaderTableSize) {
          VLOG(2) << "Limiting table size from " << tableSize << " to " <<
            http2::kMaxHeaderTableSize;
          tableSize = http2::kMaxHeaderTableSize;
        }
        headerCodec_.setEncoderHeaderTableSize(tableSize);
      }
      break;
      case SettingsId::ENABLE_PUSH:
        if ((setting.second != 0 && setting.second != 1) ||
            (setting.second == 1 &&
             transportDirection_ == TransportDirection::UPSTREAM)) {
          goawayErrorMessage_ = folly::to<string>(
              "GOAWAY error: ENABLE_PUSH invalid setting=", setting.second,
              " for streamID=", curHeader_.stream);
          VLOG(4) << goawayErrorMessage_;
          return ErrorCode::PROTOCOL_ERROR;
        }
        break;
      case SettingsId::MAX_CONCURRENT_STREAMS:
        break;
      case SettingsId::INITIAL_WINDOW_SIZE:
        if (setting.second > http2::kMaxWindowUpdateSize) {
          goawayErrorMessage_ = folly::to<string>(
              "GOAWAY error: INITIAL_WINDOW_SIZE invalid size=", setting.second,
              " for streamID=", curHeader_.stream);
          VLOG(4) << goawayErrorMessage_;
          return ErrorCode::PROTOCOL_ERROR;
        }
        break;
      case SettingsId::MAX_FRAME_SIZE:
        if (setting.second < http2::kMaxFramePayloadLengthMin ||
            setting.second > http2::kMaxFramePayloadLength) {
          goawayErrorMessage_ = folly::to<string>(
              "GOAWAY error: MAX_FRAME_SIZE invalid size=", setting.second,
              " for streamID=", curHeader_.stream);
          VLOG(4) << goawayErrorMessage_;
          return ErrorCode::PROTOCOL_ERROR;
        }
        ingressSettings_.setSetting(SettingsId::MAX_FRAME_SIZE, setting.second);
        break;
      case SettingsId::MAX_HEADER_LIST_SIZE:
        break;
      case SettingsId::ENABLE_EX_HEADERS:
      {
        auto ptr = egressSettings_.getSetting(SettingsId::ENABLE_EX_HEADERS);
        if (ptr && ptr->value > 0) {
          VLOG(4) << getTransportDirectionString(getTransportDirection())
                  << " got ENABLE_EX_HEADERS=" << setting.second;
          if (setting.second != 0 && setting.second != 1) {
            goawayErrorMessage_ = folly::to<string>(
              "GOAWAY error: invalid ENABLE_EX_HEADERS=", setting.second,
              " for streamID=", curHeader_.stream);
            VLOG(4) << goawayErrorMessage_;
            return ErrorCode::PROTOCOL_ERROR;
          }
          break;
        } else {
          // egress ENABLE_EX_HEADERS is disabled, consider the ingress
          // ENABLE_EX_HEADERS as unknown setting, and ignore it.
          continue;
        }
      }
      case SettingsId::ENABLE_CONNECT_PROTOCOL:
        if (setting.second > 1) {
          goawayErrorMessage_ = folly::to<string>(
              "GOAWAY error: ENABLE_CONNECT_PROTOCOL invalid number=",
              setting.second, " for streamID=", curHeader_.stream);
          VLOG(4) << goawayErrorMessage_;
          return ErrorCode::PROTOCOL_ERROR;
        }
        break;
      case SettingsId::THRIFT_CHANNEL_ID:
      case SettingsId::THRIFT_CHANNEL_ID_DEPRECATED:
        break;
      case SettingsId::SETTINGS_HTTP_CERT_AUTH:
        break;
      default:
        continue; // ignore unknown setting
    }
    ingressSettings_.setSetting(setting.first, setting.second);
    settingsList.push_back(*ingressSettings_.getSetting(setting.first));
  }
  if (callback_) {
    callback_->onSettings(settingsList);
  }
  return ErrorCode::NO_ERROR;
}

ErrorCode HTTP2Codec::parsePushPromise(Cursor& cursor) {
  // stream id must be idle - protocol error
  // assoc-stream-id=closed/unknown - protocol error, unless rst_stream sent

  /*
   * What does "must handle" mean in the following context?  I have to
   * accept this as a valid pushed resource?

    However, an endpoint that has sent RST_STREAM on the associated
    stream MUST handle PUSH_PROMISE frames that might have been
    created before the RST_STREAM frame is received and processed.
  */
  if (transportDirection_ != TransportDirection::UPSTREAM) {
    goawayErrorMessage_ = folly::to<string>(
      "Received PUSH_PROMISE on DOWNSTREAM codec");
    VLOG(2) << goawayErrorMessage_;
    return ErrorCode::PROTOCOL_ERROR;
  }
  if (egressSettings_.getSetting(SettingsId::ENABLE_PUSH, -1) != 1) {
    goawayErrorMessage_ = folly::to<string>(
      "Received PUSH_PROMISE on codec with push disabled");
    VLOG(2) << goawayErrorMessage_;
    return ErrorCode::PROTOCOL_ERROR;
  }
  VLOG(4) << "parsing PUSH_PROMISE frame for stream=" << curHeader_.stream <<
    " length=" << curHeader_.length;
  uint32_t promisedStream;
  std::unique_ptr<IOBuf> headerBlockFragment;
  auto err = http2::parsePushPromise(cursor, curHeader_, promisedStream,
                                     headerBlockFragment);
  RETURN_IF_ERROR(err);
  RETURN_IF_ERROR(checkNewStream(promisedStream, false /* trailersAllowed */));
  err = parseHeadersImpl(cursor, std::move(headerBlockFragment), folly::none,
                         promisedStream, folly::none);
  return err;
}

ErrorCode HTTP2Codec::parsePing(Cursor& cursor) {
  VLOG(4) << "parsing PING frame length=" << curHeader_.length;
  uint64_t opaqueData = 0;
  auto err = http2::parsePing(cursor, curHeader_, opaqueData);
  RETURN_IF_ERROR(err);
  if (callback_) {
    if (curHeader_.flags & http2::ACK) {
      callback_->onPingReply(opaqueData);
    } else {
      callback_->onPingRequest(opaqueData);
    }
  }
  return ErrorCode::NO_ERROR;
}

ErrorCode HTTP2Codec::parseGoaway(Cursor& cursor) {
  VLOG(4) << "parsing GOAWAY frame length=" << curHeader_.length;
  uint32_t lastGoodStream = 0;
  ErrorCode statusCode = ErrorCode::NO_ERROR;
  std::unique_ptr<IOBuf> debugData;

  auto err = http2::parseGoaway(cursor, curHeader_, lastGoodStream, statusCode,
                                debugData);
  if (statusCode != ErrorCode::NO_ERROR) {
    VLOG(2) << "Goaway error statusCode=" << getErrorCodeString(statusCode)
            << " lastStream=" << lastGoodStream
            << " user-agent=" << userAgent_ <<  " debugData=" <<
      ((debugData) ? string((char*)debugData->data(), debugData->length()):
       empty_string);
  }
  RETURN_IF_ERROR(err);
  if (lastGoodStream < ingressGoawayAck_) {
    ingressGoawayAck_ = lastGoodStream;
    // Drain all streams <= lastGoodStream
    // and abort streams > lastGoodStream
    if (callback_) {
      callback_->onGoaway(lastGoodStream, statusCode, std::move(debugData));
    }
  } else {
    LOG(WARNING) << "Received multiple GOAWAY with increasing ack";
  }
  return ErrorCode::NO_ERROR;
}

ErrorCode HTTP2Codec::parseWindowUpdate(Cursor& cursor) {
  VLOG(4) << "parsing WINDOW_UPDATE frame for stream=" << curHeader_.stream <<
    " length=" << curHeader_.length;
  uint32_t delta = 0;
  auto err = http2::parseWindowUpdate(cursor, curHeader_, delta);
  RETURN_IF_ERROR(err);
  if (delta == 0) {
    VLOG(4) << "Invalid 0 length delta for stream=" << curHeader_.stream;
    if (curHeader_.stream == 0) {
      goawayErrorMessage_ = folly::to<string>(
        "GOAWAY error: invalid/0 length delta for streamID=",
        curHeader_.stream);
      return ErrorCode::PROTOCOL_ERROR;
    } else {
      // Parsing a zero delta window update should cause a protocol error
      // and send a rst stream
      goawayErrorMessage_ = folly::to<string>(
        "parseWindowUpdate Invalid 0 length");
      VLOG(4) << goawayErrorMessage_;
      streamError(folly::to<std::string>("streamID=", curHeader_.stream,
                                         " with HTTP2Codec stream error: ",
                                         "window update delta=", delta),
                  ErrorCode::PROTOCOL_ERROR);
      return ErrorCode::PROTOCOL_ERROR;
    }
  }
  // if window exceeds 2^31-1, connection/stream error flow control error
  // must be checked in session/txn
  deliverCallbackIfAllowed(&HTTPCodec::Callback::onWindowUpdate,
                           "onWindowUpdate", curHeader_.stream, delta);
  return ErrorCode::NO_ERROR;
}

ErrorCode HTTP2Codec::parseCertificateRequest(Cursor& cursor) {
  VLOG(4) << "parsing CERTIFICATE_REQUEST frame length=" << curHeader_.length;
  uint16_t requestId = 0;
  std::unique_ptr<IOBuf> authRequest;

  auto err = http2::parseCertificateRequest(
      cursor, curHeader_, requestId, authRequest);
  RETURN_IF_ERROR(err);
  if (callback_) {
    callback_->onCertificateRequest(requestId, std::move(authRequest));
  }
  return ErrorCode::NO_ERROR;
}

ErrorCode HTTP2Codec::parseCertificate(Cursor& cursor) {
  VLOG(4) << "parsing CERTIFICATE frame length=" << curHeader_.length;
  uint16_t certId = 0;
  std::unique_ptr<IOBuf> authData;
  auto err = http2::parseCertificate(cursor, curHeader_, certId, authData);
  RETURN_IF_ERROR(err);
  if (curAuthenticatorBlock_.empty()) {
    curCertId_ = certId;
  } else if (certId != curCertId_) {
    // Received CERTIFICATE frame with different Cert-ID.
    return ErrorCode::PROTOCOL_ERROR;
  }
  curAuthenticatorBlock_.append(std::move(authData));
  if (curAuthenticatorBlock_.chainLength() > http2::kMaxAuthenticatorBufSize) {
    // Received excessively long authenticator.
    return ErrorCode::PROTOCOL_ERROR;
  }
  if (!(curHeader_.flags & http2::TO_BE_CONTINUED)) {
    auto authenticator = curAuthenticatorBlock_.move();
    if (callback_) {
      callback_->onCertificate(certId, std::move(authenticator));
    } else {
      curAuthenticatorBlock_.clear();
    }
  }
  return ErrorCode::NO_ERROR;
}

ErrorCode HTTP2Codec::checkNewStream(uint32_t streamId, bool trailersAllowed) {
  if (streamId == 0) {
    goawayErrorMessage_ = folly::to<string>(
        "GOAWAY error: received streamID=", streamId,
        " as invalid new stream for lastStreamID_=", lastStreamID_);
    VLOG(4) << goawayErrorMessage_;
    return ErrorCode::PROTOCOL_ERROR;
  }
  parsingDownstreamTrailers_ = trailersAllowed && (streamId <= lastStreamID_);
  if (parsingDownstreamTrailers_) {
    VLOG(4) << "Parsing downstream trailers streamId=" << streamId;
  }

  if (sessionClosing_ != ClosingState::CLOSED) {
    lastStreamID_ = streamId;
  }

  if (isInitiatedStream(streamId)) {
    // this stream should be initiated by us, not by peer
    goawayErrorMessage_ = folly::to<string>(
        "GOAWAY error: invalid new stream received with streamID=", streamId);
    VLOG(4) << goawayErrorMessage_;
    return ErrorCode::PROTOCOL_ERROR;
  } else {
    return ErrorCode::NO_ERROR;
  }
}

size_t HTTP2Codec::generateConnectionPreface(folly::IOBufQueue& writeBuf) {
  if (transportDirection_ == TransportDirection::UPSTREAM) {
    VLOG(4) << "generating connection preface";
    writeBuf.append(http2::kConnectionPreface);
    return http2::kConnectionPreface.length();
  }
  return 0;
}

bool HTTP2Codec::onIngressUpgradeMessage(const HTTPMessage& msg) {
  if (!HTTPParallelCodec::onIngressUpgradeMessage(msg)) {
    return false;
  }
  if (msg.getHeaders().getNumberOfValues(http2::kProtocolSettingsHeader) != 1) {
    VLOG(4) << __func__ << " with no HTTP2-Settings";
    return false;
  }

  const auto& settingsHeader = msg.getHeaders().getSingleOrEmpty(
    http2::kProtocolSettingsHeader);
  if (settingsHeader.empty()) {
    return true;
  }

  auto decoded = base64url_decode(settingsHeader);

  // Must be well formed Base64Url and not too large
  if (decoded.empty() || decoded.length() > http2::kMaxFramePayloadLength) {
    VLOG(4) << __func__ << " failed to decode HTTP2-Settings";
    return false;
  }
  std::unique_ptr<IOBuf> decodedBuf = IOBuf::wrapBuffer(decoded.data(),
                                                        decoded.length());
  IOBufQueue settingsQueue{IOBufQueue::cacheChainLength()};
  settingsQueue.append(std::move(decodedBuf));
  Cursor c(settingsQueue.front());
  std::deque<SettingPair> settings;
  // downcast is ok because of above length check
  http2::FrameHeader frameHeader{
    (uint32_t)settingsQueue.chainLength(), 0, http2::FrameType::SETTINGS, 0, 0};
  auto err = http2::parseSettings(c, frameHeader, settings);
  if (err != ErrorCode::NO_ERROR) {
    VLOG(4) << __func__ << " bad settings frame";
    return false;
  }

  if (handleSettings(settings) != ErrorCode::NO_ERROR) {
    VLOG(4) << __func__ << " handleSettings failed";
    return false;
  }

  return true;
}

void HTTP2Codec::generateHeader(folly::IOBufQueue& writeBuf,
                                StreamID stream,
                                const HTTPMessage& msg,
                                bool eom,
                                HTTPHeaderSize* size) {
  generateHeaderImpl(writeBuf,
                     stream,
                     msg,
                     folly::none, /* assocStream */
                     folly::none, /* controlStream */
                     eom,
                     size);
}

void HTTP2Codec::generatePushPromise(folly::IOBufQueue& writeBuf,
                                     StreamID stream,
                                     const HTTPMessage& msg,
                                     StreamID assocStream,
                                     bool eom,
                                     HTTPHeaderSize* size) {
  generateHeaderImpl(writeBuf,
                     stream,
                     msg,
                     assocStream,
                     folly::none, /* controlStream */
                     eom,
                     size);
}

void HTTP2Codec::generateExHeader(folly::IOBufQueue& writeBuf,
                                  StreamID stream,
                                  const HTTPMessage& msg,
                                  const HTTPCodec::ExAttributes& exAttributes,
                                  bool eom,
                                  HTTPHeaderSize* size) {
  generateHeaderImpl(writeBuf,
                     stream,
                     msg,
                     folly::none, /* assocStream */
                     exAttributes,
                     eom,
                     size);
}

void HTTP2Codec::generateHeaderImpl(
    folly::IOBufQueue& writeBuf,
    StreamID stream,
    const HTTPMessage& msg,
    const folly::Optional<StreamID>& assocStream,
    const folly::Optional<HTTPCodec::ExAttributes>& exAttributes,
    bool eom,
    HTTPHeaderSize* size) {
  if (assocStream) {
    CHECK(!exAttributes);
    VLOG(4) << "generating PUSH_PROMISE for stream=" << stream;
  } else if (exAttributes) {
    CHECK(!assocStream);
    VLOG(4) << "generating ExHEADERS for stream=" << stream
            << " with control stream=" << exAttributes->controlStream
            << " unidirectional=" << exAttributes->unidirectional;
  } else {
    VLOG(4) << "generating HEADERS for stream=" << stream;
  }

  if (!isStreamIngressEgressAllowed(stream)) {
    VLOG(2) << "Suppressing HEADERS/PROMISE for stream=" << stream <<
      " ingressGoawayAck_=" << ingressGoawayAck_;
    if (size) {
      size->uncompressed = 0;
      size->compressed = 0;
    }
    return;
  }

  if (msg.isRequest()) {
    DCHECK(transportDirection_ == TransportDirection::UPSTREAM ||
           assocStream || exAttributes);
    if (msg.isEgressWebsocketUpgrade()) {
      upgradedStreams_.insert(stream);
    }
  } else {
    DCHECK(transportDirection_ == TransportDirection::DOWNSTREAM ||
           exAttributes);
  }

  std::vector<std::string> temps;
  auto allHeaders = CodecUtil::prepareMessageForCompression(msg, temps);
  auto out = encodeHeaders(msg.getHeaders(), allHeaders, size);
  IOBufQueue queue(IOBufQueue::cacheChainLength());
  queue.append(std::move(out));
  auto maxFrameSize = maxSendFrameSize();
  if (queue.chainLength() > 0) {
    folly::Optional<http2::PriorityUpdate> pri;
    auto res = msg.getHTTP2Priority();
    auto remainingFrameSize = maxFrameSize;
    if (res) {
      pri = http2::PriorityUpdate{std::get<0>(*res), std::get<1>(*res),
                                  std::get<2>(*res)};
      DCHECK_GE(remainingFrameSize, http2::kFramePrioritySize)
        << "no enough space for priority? frameHeadroom=" << remainingFrameSize;
      remainingFrameSize -= http2::kFramePrioritySize;
    }
    auto chunk = queue.split(std::min(remainingFrameSize, queue.chainLength()));

    bool endHeaders = queue.chainLength() == 0;

    if (assocStream) {
      DCHECK_EQ(transportDirection_, TransportDirection::DOWNSTREAM);
      DCHECK(!eom);
      generateHeaderCallbackWrapper(stream, http2::FrameType::PUSH_PROMISE,
                                    http2::writePushPromise(writeBuf,
                                                            *assocStream,
                                                            stream,
                                                            std::move(chunk),
                                                            http2::kNoPadding,
                                                            endHeaders));
    } else if (exAttributes) {
      generateHeaderCallbackWrapper(
        stream,
        http2::FrameType::EX_HEADERS,
        http2::writeExHeaders(writeBuf,
                              std::move(chunk),
                              stream,
                              *exAttributes,
                              pri,
                              http2::kNoPadding,
                              eom,
                              endHeaders));
    } else {
      generateHeaderCallbackWrapper(stream, http2::FrameType::HEADERS,
                                    http2::writeHeaders(writeBuf,
                                                        std::move(chunk),
                                                        stream,
                                                        pri,
                                                        http2::kNoPadding,
                                                        eom,
                                                        endHeaders));
    }

    if (!endHeaders) {
      generateContinuation(writeBuf, queue, stream, maxFrameSize);
    }
  }
}

void HTTP2Codec::generateContinuation(folly::IOBufQueue& writeBuf,
                                      folly::IOBufQueue& queue,
                                      StreamID stream,
                                      size_t maxFrameSize) {
  bool endHeaders = false;
  while (!endHeaders) {
    auto chunk = queue.split(std::min(maxFrameSize, queue.chainLength()));
    endHeaders = (queue.chainLength() == 0);
    VLOG(4) << "generating CONTINUATION for stream=" << stream;
    generateHeaderCallbackWrapper(
        stream,
        http2::FrameType::CONTINUATION,
        http2::writeContinuation(
            writeBuf, stream, endHeaders, std::move(chunk)));
  }
}

std::unique_ptr<folly::IOBuf> HTTP2Codec::encodeHeaders(
    const HTTPHeaders& headers,
    std::vector<compress::Header>& allHeaders,
    HTTPHeaderSize* size) {
  headerCodec_.setEncodeHeadroom(http2::kFrameHeaderSize +
                                 http2::kFrameHeadersBaseMaxSize);
  auto out = headerCodec_.encode(allHeaders);
  if (size) {
    *size = headerCodec_.getEncodedSize();
  }

  if (headerCodec_.getEncodedSize().uncompressed >
      ingressSettings_.getSetting(SettingsId::MAX_HEADER_LIST_SIZE,
                                  std::numeric_limits<uint32_t>::max())) {
    // The remote side told us they don't want headers this large...
    // but this function has no mechanism to fail
    string serializedHeaders;
    headers.forEach(
      [&serializedHeaders] (const string& name, const string& value) {
        serializedHeaders = folly::to<string>(serializedHeaders, "\\n", name,
                                              ":", value);
      });
    LOG(ERROR) << "generating HEADERS frame larger than peer maximum nHeaders="
               << headers.size() << " all headers="
               << serializedHeaders;
  }
  return out;
}

size_t HTTP2Codec::generateHeaderCallbackWrapper(StreamID stream,
                                                 http2::FrameType type,
                                                 size_t length) {
  if (callback_) {
    callback_->onGenerateFrameHeader(stream,
                                     static_cast<uint8_t>(type),
                                     length);
  }
  return length;
}

size_t HTTP2Codec::generateBody(folly::IOBufQueue& writeBuf,
                                StreamID stream,
                                std::unique_ptr<folly::IOBuf> chain,
                                folly::Optional<uint8_t> padding,
                                bool eom) {
  // todo: generate random padding for everything?
  size_t written = 0;
  if (!isStreamIngressEgressAllowed(stream)) {
    VLOG(2) << "Suppressing DATA for stream=" << stream << " ingressGoawayAck_="
            << ingressGoawayAck_;
    return 0;
  }
  IOBufQueue queue(IOBufQueue::cacheChainLength());
  queue.append(std::move(chain));
  size_t maxFrameSize = maxSendFrameSize();
  while (queue.chainLength() > maxFrameSize) {
    auto chunk = queue.split(maxFrameSize);
    written += generateHeaderCallbackWrapper(
                  stream,
                  http2::FrameType::DATA,
                  http2::writeData(writeBuf,
                                   std::move(chunk),
                                   stream,
                                   padding,
                                   false,
                                   reuseIOBufHeadroomForData_));
  }

  return written + generateHeaderCallbackWrapper(
                      stream,
                      http2::FrameType::DATA,
                      http2::writeData(writeBuf,
                                       queue.move(),
                                       stream,
                                       padding,
                                       eom,
                                       reuseIOBufHeadroomForData_));
}

size_t HTTP2Codec::generateChunkHeader(folly::IOBufQueue& /*writeBuf*/,
                                       StreamID /*stream*/,
                                       size_t /*length*/) {
  // HTTP/2 has no chunk headers
  return 0;
}

size_t HTTP2Codec::generateChunkTerminator(folly::IOBufQueue& /*writeBuf*/,
                                           StreamID /*stream*/) {
  // HTTP/2 has no chunk terminators
  return 0;
}

size_t HTTP2Codec::generateTrailers(folly::IOBufQueue& writeBuf,
                                    StreamID stream,
                                    const HTTPHeaders& trailers) {
  std::vector<compress::Header> allHeaders;
  CodecUtil::appendHeaders(trailers, allHeaders, HTTP_HEADER_NONE);

  HTTPHeaderSize size;
  auto out = encodeHeaders(trailers, allHeaders, &size);

  IOBufQueue queue(IOBufQueue::cacheChainLength());
  queue.append(std::move(out));
  auto maxFrameSize = maxSendFrameSize();
  if (queue.chainLength() > 0) {
    folly::Optional<http2::PriorityUpdate> pri;
    auto remainingFrameSize = maxFrameSize;
    auto chunk = queue.split(std::min(remainingFrameSize, queue.chainLength()));
    bool endHeaders = queue.chainLength() == 0;
    generateHeaderCallbackWrapper(stream,
                                  http2::FrameType::HEADERS,
                                  http2::writeHeaders(writeBuf,
                                                      std::move(chunk),
                                                      stream,
                                                      pri,
                                                      http2::kNoPadding,
                                                      true /*eom*/,
                                                      endHeaders));

    if (!endHeaders) {
      generateContinuation(writeBuf, queue, stream, maxFrameSize);
    }
  }

  return size.compressed;
}

size_t HTTP2Codec::generateEOM(folly::IOBufQueue& writeBuf,
                               StreamID stream) {
  VLOG(4) << "sending EOM for stream=" << stream;
  upgradedStreams_.erase(stream);
  if (!isStreamIngressEgressAllowed(stream)) {
    VLOG(2) << "suppressed EOM for stream=" << stream << " ingressGoawayAck_="
            << ingressGoawayAck_;
    return 0;
  }
  return generateHeaderCallbackWrapper(
            stream,
            http2::FrameType::DATA,
            http2::writeData(writeBuf,
                             nullptr,
                             stream,
                             http2::kNoPadding,
                             true,
                             reuseIOBufHeadroomForData_));
}

size_t HTTP2Codec::generateRstStream(folly::IOBufQueue& writeBuf,
                                     StreamID stream,
                                     ErrorCode statusCode) {
  VLOG(4) << "sending RST_STREAM for stream=" << stream
          << " with code=" << getErrorCodeString(statusCode);
  if (!isStreamIngressEgressAllowed(stream)) {
    VLOG(2) << "suppressed RST_STREAM for stream=" << stream
            << " ingressGoawayAck_=" << ingressGoawayAck_;
    return 0;
  }
  // Suppress any EOM callback for the current frame.
  if (stream == curHeader_.stream) {
    curHeader_.flags &= ~http2::END_STREAM;
    pendingEndStreamHandling_ = false;
    ingressWebsocketUpgrade_ = false;
  }
  upgradedStreams_.erase(stream);

  if (statusCode == ErrorCode::PROTOCOL_ERROR) {
    VLOG(2) << "sending RST_STREAM with code=" << getErrorCodeString(statusCode)
            << " for stream=" << stream << " user-agent=" << userAgent_;
  }
  auto code = http2::errorCodeToReset(statusCode);
  return generateHeaderCallbackWrapper(stream, http2::FrameType::RST_STREAM,
                                       http2::writeRstStream(writeBuf, stream, code));
}

size_t HTTP2Codec::generateGoaway(folly::IOBufQueue& writeBuf,
                                  StreamID lastStream,
                                  ErrorCode statusCode,
                                  std::unique_ptr<folly::IOBuf> debugData) {
  DCHECK_LE(lastStream, egressGoawayAck_) << "Cannot increase last good stream";
  egressGoawayAck_ = lastStream;
  if (sessionClosing_ == ClosingState::CLOSED) {
    VLOG(4) << "Not sending GOAWAY for closed session";
    return 0;
  }
  switch (sessionClosing_) {
    case ClosingState::OPEN:
    case ClosingState::OPEN_WITH_GRACEFUL_DRAIN_ENABLED:
      if (lastStream == std::numeric_limits<int32_t>::max() &&
          statusCode == ErrorCode::NO_ERROR) {
        sessionClosing_ = ClosingState::FIRST_GOAWAY_SENT;
      } else {
        // The user of this codec decided not to do the double goaway
        // drain, or this is not a graceful shutdown
        sessionClosing_ = ClosingState::CLOSED;
      }
      break;
    case ClosingState::FIRST_GOAWAY_SENT:
      sessionClosing_ = ClosingState::CLOSED;
      break;
    case ClosingState::CLOSING:
    case ClosingState::CLOSED:
      LOG(FATAL) << "unreachable";
  }

  VLOG(4) << "Sending GOAWAY with last acknowledged stream="
          << lastStream << " with code=" << getErrorCodeString(statusCode);
  if (statusCode == ErrorCode::PROTOCOL_ERROR) {
    VLOG(2) << "sending GOAWAY with last acknowledged stream=" << lastStream
            << " with code=" << getErrorCodeString(statusCode)
            << " user-agent=" << userAgent_;
  }

  auto code = http2::errorCodeToGoaway(statusCode);
  return generateHeaderCallbackWrapper(
            0,
            http2::FrameType::GOAWAY,
            http2::writeGoaway(writeBuf,
                              lastStream,
                              code,
                              std::move(debugData)));
}

size_t HTTP2Codec::generatePingRequest(folly::IOBufQueue& writeBuf) {
  // should probably let the caller specify when integrating with session
  // we know HTTPSession sets up events to track ping latency
  uint64_t opaqueData = folly::Random::rand64();
  VLOG(4) << "Generating ping request with opaqueData=" << opaqueData;
  return generateHeaderCallbackWrapper(0, http2::FrameType::PING,
                                       http2::writePing(writeBuf, opaqueData, false /* no ack */));
}

size_t HTTP2Codec::generatePingReply(folly::IOBufQueue& writeBuf,
                                     uint64_t uniqueID) {
  VLOG(4) << "Generating ping reply with opaqueData=" << uniqueID;
  return generateHeaderCallbackWrapper(0, http2::FrameType::PING,
                                       http2::writePing(writeBuf, uniqueID, true /* ack */));
}

size_t HTTP2Codec::generateSettings(folly::IOBufQueue& writeBuf) {
  std::deque<SettingPair> settings;
  for (auto& setting: egressSettings_.getAllSettings()) {
    switch (setting.id) {
      case SettingsId::HEADER_TABLE_SIZE:
        if (pendingTableMaxSize_) {
          LOG(ERROR) << "Can't have more than one settings in flight, skipping";
          continue;
        } else {
          pendingTableMaxSize_ = setting.value;
        }
        break;
      case SettingsId::ENABLE_PUSH:
        if (transportDirection_ == TransportDirection::DOWNSTREAM) {
          // HTTP/2 spec says downstream must not send this flag
          // HTTP2Codec uses it to determine if push features are enabled
          continue;
        } else {
          CHECK(setting.value == 0 || setting.value == 1);
        }
        break;
      case SettingsId::MAX_CONCURRENT_STREAMS:
      case SettingsId::INITIAL_WINDOW_SIZE:
      case SettingsId::MAX_FRAME_SIZE:
        break;
      case SettingsId::MAX_HEADER_LIST_SIZE:
        headerCodec_.setMaxUncompressed(setting.value);
        break;
      case SettingsId::ENABLE_EX_HEADERS:
        CHECK(setting.value == 0 || setting.value == 1);
        if (setting.value == 0) {
          continue; // just skip the experimental setting if disabled
        } else {
          VLOG(4) << "generating ENABLE_EX_HEADERS=" << setting.value;
        }
        break;
      case SettingsId::ENABLE_CONNECT_PROTOCOL:
        if (setting.value == 0) {
          continue;
        }
        break;
      case SettingsId::THRIFT_CHANNEL_ID:
      case SettingsId::THRIFT_CHANNEL_ID_DEPRECATED:
        break;
      default:
        LOG(ERROR) << "ignore unknown settingsId="
                   << std::underlying_type<SettingsId>::type(setting.id)
                   << " value=" << setting.value;
        continue;
    }

    settings.push_back(SettingPair(setting.id, setting.value));
  }
  VLOG(4) << getTransportDirectionString(getTransportDirection())
          << " generating " << (unsigned)settings.size() << " settings";
  return generateHeaderCallbackWrapper(0, http2::FrameType::SETTINGS,
                                       http2::writeSettings(writeBuf, settings));
}

void HTTP2Codec::requestUpgrade(HTTPMessage& request) {
  static folly::ThreadLocalPtr<HTTP2Codec> defaultCodec;
  if (!defaultCodec.get()) {
    defaultCodec.reset(new HTTP2Codec(TransportDirection::UPSTREAM));
  }

  auto& headers = request.getHeaders();
  headers.set(HTTP_HEADER_UPGRADE, http2::kProtocolCleartextString);
  if (!request.checkForHeaderToken(HTTP_HEADER_CONNECTION, "Upgrade", false)) {
    headers.add(HTTP_HEADER_CONNECTION, "Upgrade");
  }
  IOBufQueue writeBuf{IOBufQueue::cacheChainLength()};
  defaultCodec->generateSettings(writeBuf);
  // fake an ack since defaultCodec gets reused
  defaultCodec->handleSettingsAck();
  writeBuf.trimStart(http2::kFrameHeaderSize);
  auto buf = writeBuf.move();
  buf->coalesce();
  headers.set(http2::kProtocolSettingsHeader,
              base64url_encode(folly::ByteRange(buf->data(), buf->length())));
  if (!request.checkForHeaderToken(HTTP_HEADER_CONNECTION,
                                   http2::kProtocolSettingsHeader.c_str(),
                                   false)) {
    headers.add(HTTP_HEADER_CONNECTION, http2::kProtocolSettingsHeader);
  }
}

size_t HTTP2Codec::generateSettingsAck(folly::IOBufQueue& writeBuf) {
  VLOG(4) << getTransportDirectionString(getTransportDirection())
          << " generating settings ack";
  return generateHeaderCallbackWrapper(0, http2::FrameType::SETTINGS,
                                       http2::writeSettingsAck(writeBuf));
}

size_t HTTP2Codec::generateWindowUpdate(folly::IOBufQueue& writeBuf,
                                        StreamID stream,
                                        uint32_t delta) {
  VLOG(4) << "generating window update for stream=" << stream
          << ": Processed " << delta << " bytes";
  if (!isStreamIngressEgressAllowed(stream)) {
    VLOG(2) << "suppressed WINDOW_UPDATE for stream=" << stream
            << " ingressGoawayAck_=" << ingressGoawayAck_;
    return 0;
  }
  return generateHeaderCallbackWrapper(stream, http2::FrameType::WINDOW_UPDATE,
                                       http2::writeWindowUpdate(writeBuf, stream, delta));
}

size_t HTTP2Codec::generatePriority(folly::IOBufQueue& writeBuf,
                                    StreamID stream,
                                    const HTTPMessage::HTTPPriority& pri) {
  VLOG(4) << "generating priority for stream=" << stream;
  if (!isStreamIngressEgressAllowed(stream)) {
    VLOG(2) << "suppressed PRIORITY for stream=" << stream
            << " ingressGoawayAck_=" << ingressGoawayAck_;
    return 0;
  }
  return generateHeaderCallbackWrapper(
            stream,
            http2::FrameType::PRIORITY,
            http2::writePriority(writeBuf, stream,
                                 {std::get<0>(pri),
                                   std::get<1>(pri),
                                   std::get<2>(pri)}));
}

size_t HTTP2Codec::generateCertificateRequest(
    folly::IOBufQueue& writeBuf,
    uint16_t requestId,
    std::unique_ptr<folly::IOBuf> certificateRequestData) {
  VLOG(4) << "generating CERTIFICATE_REQUEST with Request-ID=" << requestId;
  return http2::writeCertificateRequest(
      writeBuf, requestId, std::move(certificateRequestData));
}

size_t HTTP2Codec::generateCertificate(folly::IOBufQueue& writeBuf,
                                       uint16_t certId,
                                       std::unique_ptr<folly::IOBuf> certData) {
  size_t written = 0;
  VLOG(4) << "sending CERTIFICATE with Cert-ID=" << certId << "for stream=0";
  IOBufQueue queue(IOBufQueue::cacheChainLength());
  queue.append(std::move(certData));
  // The maximum size of an autenticator fragment, combined with the Cert-ID can
  // not exceed the maximal allowable size of a sent frame.
  size_t maxChunkSize = maxSendFrameSize() - sizeof(certId);
  while (queue.chainLength() > maxChunkSize) {
    auto chunk = queue.splitAtMost(maxChunkSize);
    written +=
        http2::writeCertificate(writeBuf, certId, std::move(chunk), true);
  }
  return written +
         http2::writeCertificate(writeBuf, certId, queue.move(), false);
}

bool HTTP2Codec::checkConnectionError(ErrorCode err, const folly::IOBuf* buf) {
  if (err != ErrorCode::NO_ERROR) {
    LOG(ERROR) << "Connection error " << getErrorCodeString(err)
               << " with ingress=";
    VLOG(3) << IOBufPrinter::printHexFolly(buf, true);
    if (callback_) {
      std::string errorDescription = goawayErrorMessage_.empty() ?
        "Connection error" : goawayErrorMessage_;
      HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
                       errorDescription);
      ex.setCodecStatusCode(err);
      callback_->onError(0, ex, false);
    }
    return true;
  }
  return false;
}

void HTTP2Codec::streamError(const std::string& msg, ErrorCode code,
                             bool newTxn) {
  HTTPException error(HTTPException::Direction::INGRESS_AND_EGRESS,
                      msg);
  error.setCodecStatusCode(code);
  if (callback_) {
    callback_->onError(curHeader_.stream, error, newTxn);
  }
}

HTTPCodec::StreamID
HTTP2Codec::mapPriorityToDependency(uint8_t priority) const {
  // If the priority is out of the maximum index of virtual nodes array, we
  // return the lowest level virtual node as a punishment of not setting
  // priority correctly.
  return virtualPriorityNodes_.empty()
    ? 0
    : virtualPriorityNodes_[
        std::min(priority, uint8_t(virtualPriorityNodes_.size() - 1))];
}

bool HTTP2Codec::parsingTrailers() const {
  // HEADERS frame is used for request/response headers and trailers.
  // Per spec, specific role of HEADERS frame is determined by it's postion
  // within the stream. We don't keep full stream state in this codec,
  // thus using heuristics to distinguish between headers/trailers.
  // For DOWNSTREAM case, request headers HEADERS frame would be creating
  // new stream, thus HEADERS on existing stream ID are considered trailers
  // (see checkNewStream).
  // For UPSTREAM case, response headers are required to have status code,
  // thus if no status code we consider that trailers.
  if (curHeader_.type == http2::FrameType::HEADERS ||
      curHeader_.type == http2::FrameType::CONTINUATION) {
    if (transportDirection_ == TransportDirection::DOWNSTREAM) {
      return parsingDownstreamTrailers_;
    } else {
      return !decodeInfo_.hasStatus();
    }
  }
  return false;
}
}
