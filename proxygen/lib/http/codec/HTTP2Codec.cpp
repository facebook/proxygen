/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/HTTP2Codec.h>
#include <proxygen/lib/http/codec/HTTP2Constants.h>
#include <proxygen/lib/http/codec/SPDYUtil.h>
#include <proxygen/lib/utils/ChromeUtils.h>
#include <proxygen/lib/utils/Logging.h>
#include <proxygen/lib/utils/Base64.h>

#include <folly/Conv.h>
#include <folly/io/Cursor.h>
#include <folly/Random.h>

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

uint32_t HTTP2Codec::kHeaderSplitSize{http2::kMaxFramePayloadLengthMin};

HTTP2Codec::HTTP2Codec(TransportDirection direction)
    : HTTPParallelCodec(direction),
      headerCodec_(direction),
      frameState_(direction == TransportDirection::DOWNSTREAM
                      ? FrameState::UPSTREAM_CONNECTION_PREFACE
                      : FrameState::DOWNSTREAM_CONNECTION_PREFACE),
      decodeInfo_(HTTPRequestVerifier()) {

  headerCodec_.setDecoderHeaderTableMaxSize(
    egressSettings_.getSetting(SettingsId::HEADER_TABLE_SIZE, 0));
  headerCodec_.setMaxUncompressed(
    egressSettings_.getSetting(SettingsId::MAX_HEADER_LIST_SIZE, 0));

  VLOG(4) << "creating " << getTransportDirectionString(direction)
          << " HTTP/2 codec";
}

HTTP2Codec::~HTTP2Codec() {}

// HTTPCodec API

size_t HTTP2Codec::onIngress(const folly::IOBuf& buf) {
  // TODO: ensure only 1 parse at a time on stack.

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

  if (callback_) {
    callback_->onFrameHeader(curHeader_.stream,
                             curHeader_.flags,
                             curHeader_.length);
  }

  ErrorCode err = ErrorCode::NO_ERROR;
  switch (curHeader_.type) {
    case http2::FrameType::DATA: err = parseAllData(cursor); break;
    case http2::FrameType::HEADERS: err = parseHeaders(cursor); break;
    case http2::FrameType::PRIORITY: err = parsePriority(cursor); break;
    case http2::FrameType::RST_STREAM:
      err = parseRstStream(cursor); break;
    case http2::FrameType::SETTINGS:
      err = parseSettings(cursor); break;
    case http2::FrameType::PUSH_PROMISE:
      err = parsePushPromise(cursor); break;
    case http2::FrameType::PING: err = parsePing(cursor); break;
    case http2::FrameType::GOAWAY: err = parseGoaway(cursor); break;
    case http2::FrameType::WINDOW_UPDATE:
      err = parseWindowUpdate(cursor); break;
    case http2::FrameType::CONTINUATION: err = parseContinuation(cursor); break;
    case http2::FrameType::ALTSVC:
      // fall through, unimplemented
    default:
      // Implementations MUST ignore and discard any frame that has a
      // type that is unknown
      VLOG(2) << "Skipping unknown frame type=" << (uint8_t)curHeader_.type;
      cursor.skip(curHeader_.length);
  }
  return err;
}

ErrorCode HTTP2Codec::handleEndStream() {
  if (curHeader_.type != http2::FrameType::HEADERS &&
      curHeader_.type != http2::FrameType::CONTINUATION &&
      curHeader_.type != http2::FrameType::DATA) {
    return ErrorCode::NO_ERROR;
  }

  // do we need to handle case where this stream has already aborted via
  // another callback (onHeadersComplete/onBody)?
  pendingEndStreamHandling_ |= (curHeader_.flags & http2::END_STREAM);
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
      outData = folly::make_unique<IOBuf>();
    }
    deliverCallbackIfAllowed(&HTTPCodec::Callback::onBody, "onBody",
                             curHeader_.stream, std::move(outData), padding);
  }
  return handleEndStream();
}

ErrorCode HTTP2Codec::parseDataFrameData(Cursor& cursor,
                                         size_t bufLen,
                                         size_t& parsed) {
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
      outData = folly::make_unique<IOBuf>();
    }
    deliverCallbackIfAllowed(&HTTPCodec::Callback::onBody, "onBody",
                             curHeader_.stream, std::move(outData), padding);
  }
  return (pendingDataFrameBytes_ > 0) ? ErrorCode::NO_ERROR : handleEndStream();
}


ErrorCode HTTP2Codec::parseHeaders(Cursor& cursor) {
  boost::optional<http2::PriorityUpdate> priority;
  std::unique_ptr<IOBuf> headerBuf;
  VLOG(4) << "parsing HEADERS frame for stream=" << curHeader_.stream <<
    " length=" << curHeader_.length;
  auto err = http2::parseHeaders(cursor, curHeader_, priority, headerBuf);
  RETURN_IF_ERROR(err);
  if (transportDirection_ == TransportDirection::DOWNSTREAM) {
    RETURN_IF_ERROR(checkNewStream(curHeader_.stream));
  }
  err = parseHeadersImpl(cursor, std::move(headerBuf), priority, boost::none);
  return err;
}

ErrorCode HTTP2Codec::parseContinuation(Cursor& cursor) {
  std::unique_ptr<IOBuf> headerBuf;
  VLOG(4) << "parsing CONTINUATION frame for stream=" << curHeader_.stream <<
    " length=" << curHeader_.length;
  auto err = http2::parseContinuation(cursor, curHeader_, headerBuf);
  RETURN_IF_ERROR(err);
  err = parseHeadersImpl(cursor, std::move(headerBuf),
                         boost::none, boost::none);
  return err;
}

ErrorCode HTTP2Codec::parseHeadersImpl(
  Cursor& cursor,
  std::unique_ptr<IOBuf> headerBuf,
  boost::optional<http2::PriorityUpdate> priority,
  boost::optional<uint32_t> promisedStream) {
  curHeaderBlock_.append(std::move(headerBuf));
  std::unique_ptr<HTTPMessage> msg;
  if (curHeader_.flags & http2::END_HEADERS) {
    // decompress headers
    Cursor headerCursor(curHeaderBlock_.front());
    bool isRequest = (transportDirection_ == TransportDirection::DOWNSTREAM ||
                      promisedStream);
    msg = folly::make_unique<HTTPMessage>();
    if (priority) {
      if (curHeader_.stream == priority->streamDependency) {
        streamError(folly::to<string>("Circular dependency for txn=",
                                      curHeader_.stream),
                    ErrorCode::PROTOCOL_ERROR,
                    curHeader_.type == http2::FrameType::HEADERS);
        return ErrorCode::NO_ERROR;
      }

      msg->setHTTP2Priority(std::make_tuple(priority->streamDependency,
                                            priority->exclusive,
                                            priority->weight));
    }
    decodeInfo_.init(msg.get(), isRequest);
    headerCodec_.decodeStreaming(headerCursor,
                                 curHeaderBlock_.chainLength(),
                                 this);
    // Saving this in case we need to log it on error
    folly::ScopeGuard g = folly::makeGuard([this] {
        curHeaderBlock_.move();
      });
    // Check decoding error
    if (decodeInfo_.decodeError != HeaderDecodeError::NONE) {
      LOG(ERROR) << "Failed decoding header block for stream="
                 << curHeader_.stream << " header block=" << std::endl
                 << IOBufPrinter::printHexFolly(curHeaderBlock_.front(), true);;
      return ErrorCode::COMPRESSION_ERROR;
    }
    // Check parsing error
    if (decodeInfo_.parsingError != "") {
      LOG(ERROR) << "Failed parsing header list for stream="
                 << curHeader_.stream << ", error=" << decodeInfo_.parsingError
                 << ", header block="
                 << IOBufPrinter::printHexFolly(curHeaderBlock_.front(), true);
      HTTPException err(HTTPException::Direction::INGRESS,
                        folly::to<std::string>("HTTP2Codec stream error: ",
                                               "stream=", curHeader_.stream,
                                               " status=", 400, " error: ",
                                               decodeInfo_.parsingError));
      err.setHttpStatusCode(400);
      callback_->onError(curHeader_.stream, err, true);
      return ErrorCode::NO_ERROR;
    }
    if (needsChromeWorkaround2_ &&
        curHeaderBlock_.chainLength() + http2::kFrameHeaderSize * 16 >= 16384) {
      // chrome will send a RST_STREAM/protocol error for this, convert to
      // RST_STREAM/NO_ERROR.  Note we may be off a couple bytes if the headers
      // were padded or contained a priority.
      expectedChromeResets_.insert(curHeader_.stream);
    }
  } else {
    curHeaderBlock_.append(std::move(headerBuf));
  }

  // Report back what we've parsed
  if (callback_) {
    uint32_t headersCompleteStream = curHeader_.stream;
    if (curHeader_.type == http2::FrameType::HEADERS) {
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
      if (!deliverCallbackIfAllowed(&HTTPCodec::Callback::onMessageBegin,
                                    "onMessageBegin", curHeader_.stream,
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
      callback_->onHeadersComplete(headersCompleteStream, std::move(msg));
    }
    return handleEndStream();
  }
  return ErrorCode::NO_ERROR;
}

void HTTP2Codec::onHeader(const std::string& name,
                          const std::string& value) {
  // Refuse decoding other headers if an error is already found
  if (decodeInfo_.decodeError != HeaderDecodeError::NONE
      || decodeInfo_.parsingError != "") {
    VLOG(4) << "Ignoring header=" << name << " value=" << value <<
      " due to parser error=" << decodeInfo_.parsingError;
    return;
  }
  VLOG(5) << "Processing header=" << name << " value=" << value;
  folly::StringPiece nameSp(name);
  folly::StringPiece valueSp(value);

  HTTPRequestVerifier& verifier = decodeInfo_.verifier;
  if (nameSp.startsWith(':')) {
    if (decodeInfo_.regularHeaderSeen) {
      decodeInfo_.parsingError =
        folly::to<string>("Illegal pseudo header name=", nameSp);
      return;
    }
    if (decodeInfo_.isRequest) {
      if (nameSp == http2::kMethod) {
        if (!verifier.setMethod(valueSp)) {
          return;
        }
      } else if (nameSp == http2::kScheme) {
        if (!verifier.setScheme(valueSp)) {
          return;
        }
      } else if (nameSp == http2::kAuthority) {
        if (!verifier.setAuthority(valueSp)) {
          return;
        }
      } else if (nameSp == http2::kPath) {
        if (!verifier.setPath(valueSp)) {
          return;
        }
      } else {
        decodeInfo_.parsingError =
          folly::to<string>("Invalid header name=", nameSp);
        return;
      }
    } else {
      if (nameSp == http2::kStatus) {
        if (decodeInfo_.hasStatus) {
          decodeInfo_.parsingError = string("Duplicate status");
          return;
        }
        decodeInfo_.hasStatus = true;
        int32_t code = -1;
        try {
          code = folly::to<unsigned int>(valueSp);
        } catch (const std::range_error& ex) {
        }
        if (code >= 100 && code <= 999) {
          decodeInfo_.msg->setStatusCode(code);
          decodeInfo_.msg->setStatusMessage(
              HTTPMessage::getDefaultReason(code));
        } else {
          decodeInfo_.parsingError =
            folly::to<string>("Malformed status code=", valueSp);
          return;
        }
      } else {
        decodeInfo_.parsingError =
          folly::to<string>("Invalid header name=", nameSp);
        return;
      }
    }
  } else {
    decodeInfo_.regularHeaderSeen = true;
    if (nameSp == "connection") {
      decodeInfo_.parsingError =
        string("HTTP/2 Message with Connection header");
      return;
    }
    if (nameSp == "content-length") {
      uint32_t contentLength = 0;
      try {
        contentLength = folly::to<uint32_t>(valueSp);
      } catch (const std::range_error& ex) {
      }
      if (decodeInfo_.hasContentLength &&
          contentLength != decodeInfo_.contentLength) {
        decodeInfo_.parsingError = string("Multiple content-length headers");
        return;
      }
      decodeInfo_.hasContentLength = true;
      decodeInfo_.contentLength = contentLength;
    }
    bool nameOk = SPDYUtil::validateHeaderName(nameSp);
    bool valueOk = SPDYUtil::validateHeaderValue(valueSp, SPDYUtil::STRICT);
    if (!nameOk || !valueOk) {
      decodeInfo_.parsingError = folly::to<string>("Bad header value: name=",
                                                   nameSp, " value=", valueSp);
      VLOG(4) << "dir=" << uint32_t(transportDirection_) <<
        decodeInfo_.parsingError << " codec=" << headerCodec_;
      return;
    }
    if (nameSp == "user-agent" &&
        userAgent_.empty()) {
      userAgent_ = valueSp.str();
      int8_t version = getChromeVersion(valueSp);
      if (version > 0 && version < 45) {
        needsChromeWorkaround2_ = true;
        VLOG(4) << "Using chrome http/2 16kb workaround";
      }
    }
    // Add the (name, value) pair to headers
    decodeInfo_.msg->getHeaders().add(nameSp, valueSp);
  }
}

void HTTP2Codec::onHeadersComplete() {
  HTTPHeaders& headers = decodeInfo_.msg->getHeaders();
  HTTPRequestVerifier& verifier = decodeInfo_.verifier;

  if (decodeInfo_.isRequest) {
    auto combinedCookie = headers.combine(HTTP_HEADER_COOKIE, "; ");
    if (!combinedCookie.empty()) {
      headers.set(HTTP_HEADER_COOKIE, combinedCookie);
    }
    verifier.validate();
  } else if (!decodeInfo_.hasStatus) {
    decodeInfo_.parsingError =
      string("Malformed response, missing :status");
    return;
  }
  if (!verifier.error.empty()) {
    decodeInfo_.parsingError = verifier.error;
    return;
  }
  decodeInfo_.msg->setAdvancedProtocolString(http2::kProtocolString);
  decodeInfo_.msg->setHTTPVersion(1, 1);
  decodeInfo_.msg->setIngressHeaderSize(headerCodec_.getDecodedSize());
}

void HTTP2Codec::onDecodeError(HeaderDecodeError decodeError) {
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
  ErrorCode statusCode = ErrorCode::NO_ERROR;
  auto err = http2::parseRstStream(cursor, curHeader_, statusCode);
  RETURN_IF_ERROR(err);
  if (needsChromeWorkaround2_ && statusCode == ErrorCode::PROTOCOL_ERROR) {
    auto it = expectedChromeResets_.find(curHeader_.stream);
    if (it != expectedChromeResets_.end()) {
      // convert to NO_ERROR and remove from set
      statusCode = ErrorCode::NO_ERROR;
      expectedChromeResets_.erase(it);
    }
  }
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
    // for stats
    if (callback_) {
      callback_->onSettingsAck();
    }
    return ErrorCode::NO_ERROR;
  }
  return handleSettings(settings);
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
        kHeaderSplitSize = setting.second;
        break;
      case SettingsId::MAX_HEADER_LIST_SIZE:
        break;
      default:
        // unknown setting
        break;
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
  RETURN_IF_ERROR(checkNewStream(promisedStream));
  err = parseHeadersImpl(cursor, std::move(headerBlockFragment), boost::none,
                         promisedStream);
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

ErrorCode HTTP2Codec::checkNewStream(uint32_t streamId) {
  if (streamId == 0 || streamId <= lastStreamID_) {
    goawayErrorMessage_ = folly::to<string>(
        "GOAWAY error: received streamID=", streamId,
        " as invalid new stream for lastStreamID_=", lastStreamID_);
    VLOG(4) << goawayErrorMessage_;
    return ErrorCode::PROTOCOL_ERROR;
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
                                StreamID assocStream,
                                bool eom,
                                HTTPHeaderSize* size) {
  VLOG(4) << "generating " << ((assocStream != 0) ? "PUSH_PROMISE" : "HEADERS")
          << " for stream=" << stream;
  std::vector<Header> allHeaders;

  if (!isStreamIngressEgressAllowed(stream)) {
    VLOG(2) << "Suppressing HEADERS/PROMISE for stream=" << stream <<
      " ingressGoawayAck_=" << ingressGoawayAck_;
    if (size) {
      size->uncompressed = 0;
      size->compressed = 0;
    }
    return;
  }

  // The role of this local status string is to hold the generated
  // status code long enough for the header encoding.
  std::string status;

  if (msg.isRequest()) {
    DCHECK(transportDirection_ == TransportDirection::UPSTREAM ||
           assocStream != 0);
    const string& method = msg.getMethodString();
    allHeaders.emplace_back(http2::kMethod, method);
    if (msg.getMethod() != HTTPMethod::CONNECT) {
      const string& scheme = (msg.isSecure() ? http2::kHttps : http2::kHttp);
      const string& path = msg.getURL();
      allHeaders.emplace_back(http2::kScheme, scheme);
      allHeaders.emplace_back(http2::kPath, path);
    }
    const HTTPHeaders& headers = msg.getHeaders();
    const string& host = headers.getSingleOrEmpty(HTTP_HEADER_HOST);
    if (!host.empty()) {
      allHeaders.emplace_back(http2::kAuthority, host);
    }
  } else {
    DCHECK_EQ(transportDirection_, TransportDirection::DOWNSTREAM);
    status = folly::to<string>(msg.getStatusCode());
    allHeaders.emplace_back(http2::kStatus, status);
    // HEADERS frames do not include a version or reason string.
  }

  string date;
  bool hasDateHeader = false;
  // Add the HTTP headers supplied by the caller, but skip
  // any per-hop headers that aren't supported in HTTP/2.
  msg.getHeaders().forEachWithCode(
    [&] (HTTPHeaderCode code,
         const string& name,
         const string& value) {
      static const std::bitset<256> s_perHopHeaderCodes{
        [] {
          std::bitset<256> bs;
          // HTTP/1.x per-hop headers that have no meaning in HTTP/2
          bs[HTTP_HEADER_CONNECTION] = true;
          bs[HTTP_HEADER_HOST] = true;
          bs[HTTP_HEADER_KEEP_ALIVE] = true;
          bs[HTTP_HEADER_PROXY_CONNECTION] = true;
          bs[HTTP_HEADER_TRANSFER_ENCODING] = true;
          bs[HTTP_HEADER_UPGRADE] = true;
          return bs;
        }()
      };

      if (s_perHopHeaderCodes[code] || name.size() == 0 || name[0] == ':') {
        DCHECK_GT(name.size(), 0) << "Empty header";
        DCHECK_NE(name[0], ':') << "Invalid header=" << name;
        return;
      }
      // Note this code will not drop headers named by Connection.  That's the
      // caller's job

      // see HTTP/2 spec, 8.1.2
      DCHECK(name != "TE" || value == "trailers");
      if ((name.size() > 0 && name[0] != ':') &&
          code != HTTP_HEADER_HOST) {
        allHeaders.emplace_back(code, name, value);
      }
      if (code == HTTP_HEADER_DATE) {
        hasDateHeader = true;
      }
    });

  if (msg.isResponse() && !hasDateHeader) {
    date = HTTPMessage::formatDateHeader();
    allHeaders.emplace_back(HTTP_HEADER_DATE, date);
  }
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
    LOG(ERROR) << "generating HEADERS frame larger than peer maximum";
  }

  IOBufQueue queue(IOBufQueue::cacheChainLength());
  queue.append(std::move(out));
  if (queue.chainLength() > 0) {
    boost::optional<http2::PriorityUpdate> pri;
    auto res = msg.getHTTP2Priority();
    size_t split = kHeaderSplitSize;
    if (res) {
      pri = http2::PriorityUpdate{std::get<0>(*res), std::get<1>(*res),
                                  std::get<2>(*res)};
      if (split > http2::kFramePrioritySize) {
        split -= http2::kFramePrioritySize;
      }
    }
    auto chunk = queue.split(std::min(split, queue.chainLength()));

    bool endHeaders = queue.chainLength() == 0;
    if (assocStream == 0) {
      http2::writeHeaders(writeBuf,
                          std::move(chunk),
                          stream,
                          pri,
                          http2::kNoPadding,
                          eom,
                          endHeaders);
    } else {
      DCHECK_EQ(transportDirection_, TransportDirection::DOWNSTREAM);
      DCHECK(!eom);
      http2::writePushPromise(writeBuf,
                              assocStream,
                              stream,
                              std::move(chunk),
                              http2::kNoPadding,
                              endHeaders);
    }

    while (!endHeaders) {
      chunk = queue.split(std::min(size_t(kHeaderSplitSize),
                                   queue.chainLength()));
      endHeaders = queue.chainLength() == 0;
      VLOG(4) << "generating CONTINUATION for stream=" << stream;
      http2::writeContinuation(writeBuf,
                               stream,
                               endHeaders,
                               std::move(chunk),
                               http2::kNoPadding);
    }
  }
}

size_t HTTP2Codec::generateBody(folly::IOBufQueue& writeBuf,
                                StreamID stream,
                                std::unique_ptr<folly::IOBuf> chain,
                                boost::optional<uint8_t> padding,
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
  while (queue.chainLength() > maxSendFrameSize()) {
    auto chunk = queue.split(maxSendFrameSize());
    written += http2::writeData(writeBuf, std::move(chunk), stream,
                                padding, false);
  }

  return written + http2::writeData(writeBuf, queue.move(), stream,
                                    padding, eom);
}

size_t HTTP2Codec::generateChunkHeader(folly::IOBufQueue& writeBuf,
                                       StreamID stream,
                                       size_t length) {
  // HTTP/2 has no chunk headers
  return 0;
}

size_t HTTP2Codec::generateChunkTerminator(folly::IOBufQueue& writeBuf,
                                           StreamID stream) {
  // HTTP/2 has no chunk terminators
  return 0;
}

size_t HTTP2Codec::generateTrailers(folly::IOBufQueue& writeBuf,
                                    StreamID stream,
                                    const HTTPHeaders& trailers) {
  return 0;
}

size_t HTTP2Codec::generateEOM(folly::IOBufQueue& writeBuf,
                               StreamID stream) {
  VLOG(4) << "sending EOM for stream=" << stream;
  if (!isStreamIngressEgressAllowed(stream)) {
    VLOG(2) << "suppressed EOM for stream=" << stream << " ingressGoawayAck_="
            << ingressGoawayAck_;
    return 0;
  }
  return http2::writeData(writeBuf, nullptr, stream, http2::kNoPadding, true);
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
  }

  if (statusCode == ErrorCode::PROTOCOL_ERROR) {
    VLOG(2) << "sending RST_STREAM with code=" << getErrorCodeString(statusCode)
            << " for stream=" << stream << " user-agent=" << userAgent_;
  }
  auto code = http2::errorCodeToReset(statusCode);
  return http2::writeRstStream(writeBuf, stream, code);
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
  return http2::writeGoaway(writeBuf, lastStream, code, std::move(debugData));
}

size_t HTTP2Codec::generatePingRequest(folly::IOBufQueue& writeBuf) {
  // should probably let the caller specify when integrating with session
  // we know HTTPSession sets up events to track ping latency
  uint64_t opaqueData = folly::Random::rand64();
  VLOG(4) << "Generating ping request with opaqueData=" << opaqueData;
  return http2::writePing(writeBuf, opaqueData, false /* no ack */);
}

size_t HTTP2Codec::generatePingReply(folly::IOBufQueue& writeBuf,
                                     uint64_t uniqueID) {
  VLOG(4) << "Generating ping reply with opaqueData=" << uniqueID;
  return http2::writePing(writeBuf, uniqueID, true /* ack */);
}

size_t HTTP2Codec::generateSettings(folly::IOBufQueue& writeBuf) {
  std::deque<SettingPair> settings;
  for (auto& setting: egressSettings_.getAllSettings()) {
    if (setting.isSet) {
      if (setting.id == SettingsId::HEADER_TABLE_SIZE) {
        headerCodec_.setDecoderHeaderTableMaxSize(setting.value);
      } else if (setting.id == SettingsId::MAX_HEADER_LIST_SIZE) {
        headerCodec_.setMaxUncompressed(setting.value);
      } else if (setting.id == SettingsId::ENABLE_PUSH) {
        if (transportDirection_ == TransportDirection::DOWNSTREAM) {
          // HTTP/2 spec says downstream must not send this flag
          // HTTP2Codec uses it to determine if push features are enabled
          continue;
        } else {
          CHECK(setting.value == 0 || setting.value == 1);
        }
      }
      settings.push_back(SettingPair(setting.id, setting.value));
    }
  }
  VLOG(4) << "generating " << (unsigned)settings.size() << " settings";
  return http2::writeSettings(writeBuf, settings);
}

void HTTP2Codec::requestUpgrade(HTTPMessage& request) {
  static HTTP2Codec defaultCodec(TransportDirection::UPSTREAM);

  auto& headers = request.getHeaders();
  headers.set(HTTP_HEADER_UPGRADE, http2::kProtocolCleartextString);
  if (!request.checkForHeaderToken(HTTP_HEADER_CONNECTION, "Upgrade", false)) {
    headers.add(HTTP_HEADER_CONNECTION, "Upgrade");
  }
  IOBufQueue writeBuf{IOBufQueue::cacheChainLength()};
  defaultCodec.generateSettings(writeBuf);
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
  VLOG(4) << "generating settings ack";
  return http2::writeSettingsAck(writeBuf);
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
  return http2::writeWindowUpdate(writeBuf, stream, delta);
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
  return http2::writePriority(writeBuf, stream,
                              {std::get<0>(pri), std::get<1>(pri),
                                  std::get<2>(pri)});
}

bool HTTP2Codec::checkConnectionError(ErrorCode err, const folly::IOBuf* buf) {
  if (err != ErrorCode::NO_ERROR) {
    LOG(ERROR) << "Connection error with ingress=" << std::endl
               << IOBufPrinter::printHexFolly(buf, true);
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

}
