/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/HTTPBinaryCodec.h>

#include <quic/codec/QuicInteger.h>

namespace proxygen {

HTTPBinaryCodec::HTTPBinaryCodec(TransportDirection direction) {
  transportDirection_ = direction;
  state_ = ParseState::FRAMING_INDICATOR;
  parseError_ = folly::none;
  parserPaused_ = false;
}

HTTPBinaryCodec::~HTTPBinaryCodec() {
}

ParseResult HTTPBinaryCodec::parseFramingIndicator(folly::io::Cursor& cursor,
                                                   bool& request,
                                                   bool& knownLength) {
  size_t parsed = 0;

  // Parse the framingIndicator and advance the cursor
  auto framingIndicator = quic::decodeQuicInteger(cursor);
  if (!framingIndicator) {
    return folly::makeUnexpected(
        std::string("Failure to parse Framing Indicator"));
  }
  // Increase parsed by the number of bytes read
  parsed += framingIndicator->second;
  // Sanity check the value of the framingIndicator
  if (framingIndicator->first >
      static_cast<uint64_t>(
          HTTPBinaryCodec::FramingIndicator::RESPONSE_INDETERMINATE_LENGTH)) {
    return folly::makeUnexpected(
        fmt::format("Invalid Framing Indicator: {}", framingIndicator->first));
  }

  // Set request to true if framingIndicator is even (0 and 2 correspond to
  // requests)
  request = ((framingIndicator->first & 0x01) == 0);
  // Set knownLength to true if framingIndicator is 0 or 1
  knownLength = ((framingIndicator->first & 0x02) == 0);
  if (!knownLength) {
    return folly::makeUnexpected(
        std::string("Unsupported indeterminate length Binary HTTP Request"));
  }
  return parsed;
}

ParseResult HTTPBinaryCodec::parseKnownLengthString(
    folly::io::Cursor& cursor,
    size_t remaining,
    folly::StringPiece stringName,
    std::string& stringValue) {
  size_t parsed = 0;

  // Parse the encodedStringLength and advance cursor
  auto encodedStringLength = quic::decodeQuicInteger(cursor);
  if (!encodedStringLength) {
    return folly::makeUnexpected(
        fmt::format("Failure to parse: {} length", stringName));
  }
  // Increase parsed by the number of bytes read
  parsed += encodedStringLength->second;
  // Check that we have not gone beyond "remaining"
  if (encodedStringLength->first > remaining - parsed) {
    return folly::makeUnexpected(
        fmt::format("Failure to parse: {}", stringName));
  }

  // Handle edge case where field is not present/has length 0
  if (encodedStringLength->first == 0) {
    stringValue.clear();
    return parsed;
  }

  // Read the value of the encodedString
  stringValue = cursor.readFixedString(encodedStringLength->first);

  // Increase parsed by the number of bytes read
  parsed += encodedStringLength->first;
  return parsed;
}

ParseResult HTTPBinaryCodec::parseRequestControlData(folly::io::Cursor& cursor,
                                                     size_t remaining,
                                                     HTTPMessage& msg) {
  size_t parsed = 0;

  // Parse method
  std::string method;
  auto methodRes = parseKnownLengthString(cursor, remaining, "method", method);
  if (methodRes.hasError()) {
    return methodRes;
  }
  parsed += *methodRes;
  remaining -= *methodRes;
  msg.setMethod(method);

  // Parse scheme
  std::string scheme;
  auto schemeRes = parseKnownLengthString(cursor, remaining, "scheme", scheme);
  if (schemeRes.hasError()) {
    return schemeRes;
  }
  parsed += *schemeRes;
  remaining -= *schemeRes;
  if (scheme == proxygen::headers::kHttp) {
    msg.setSecure(false);
  } else if (scheme == proxygen::headers::kHttps) {
    msg.setSecure(true);
  } else {
    return folly::makeUnexpected(
        std::string("Failure to parse: scheme. Should be 'http' or 'https'"));
  }

  // Parse authority
  std::string authority;
  auto authorityRes =
      parseKnownLengthString(cursor, remaining, "authority", authority);
  if (authorityRes.hasError()) {
    return authorityRes;
  }
  parsed += *authorityRes;
  remaining -= *authorityRes;

  // Parse path
  std::string path;
  auto pathRes = parseKnownLengthString(cursor, remaining, "path", path);
  if (pathRes.hasError()) {
    return pathRes;
  }
  // Set relative path to msg URL
  auto parseUrl = msg.setURL(path);
  if (!parseUrl.valid()) {
    return folly::makeUnexpected(
        fmt::format("Failure to parse: invalid URL path '{}'", path));
  }
  parsed += *pathRes;
  remaining -= *pathRes;

  return parsed;
}

ParseResult HTTPBinaryCodec::parseResponseControlData(folly::io::Cursor& cursor,
                                                      size_t remaining,
                                                      HTTPMessage& msg) {
  // Parse statusCode and advance cursor
  auto statusCode = quic::decodeQuicInteger(cursor);
  if (!statusCode) {
    return folly::makeUnexpected(
        std::string("Failure to parse response status code"));
  }
  // Sanity check status code
  if (statusCode->first < 200 || statusCode->first > 599) {
    return folly::makeUnexpected(
        fmt::format("Invalid response status code: {}", statusCode->first));
  }
  msg.setStatusCode(statusCode->first);
  return statusCode->second;
}

ParseResult HTTPBinaryCodec::parseHeadersHelper(folly::io::Cursor& cursor,
                                                size_t remaining,
                                                HTTPMessage& msg,
                                                bool isTrailers) {
  size_t parsed = 0;

  // Parse length of headers and advance cursor
  auto lengthOfHeaders = quic::decodeQuicInteger(cursor);
  if (!lengthOfHeaders) {
    return folly::makeUnexpected(
        std::string("Failure to parse number of headers"));
  }
  // Check that we had enough bytes to parse lengthOfHeaders
  if (remaining < lengthOfHeaders->second) {
    return folly::makeUnexpected(
        fmt::format("Header parsing underflowed! Not enough space ({} bytes "
                    "remaining) to parse the length of headers ({} bytes)",
                    remaining,
                    lengthOfHeaders->second));
  }
  // Increase parsed and decrease remaining by the number of bytes read
  parsed += lengthOfHeaders->second;
  remaining -= lengthOfHeaders->second;
  if (remaining < lengthOfHeaders->first) {
    return folly::makeUnexpected(fmt::format(
        "Header parsing underflowed! Headers length in bytes ({}) is "
        "inconsistent with remaining buffer length ({})",
        lengthOfHeaders->first,
        remaining));
  }

  // Retrieve the HTTPHeaders from msg and mutate them directly
  HTTPHeaders* headers = nullptr;
  if (isTrailers) {
    trailers_ = std::make_unique<HTTPHeaders>();
    headers = trailers_.get();
  } else {
    headers = &msg.getHeaders();
  }
  auto numHeaders = 0;
  while (parsed < lengthOfHeaders->first) {
    std::string headerName;
    auto headerNameRes =
        parseKnownLengthString(cursor, remaining, "headerName", headerName);
    if (headerNameRes.hasError()) {
      return headerNameRes;
    }
    parsed += *headerNameRes;
    remaining -= *headerNameRes;

    std::string headerValue;
    auto headerValueRes =
        parseKnownLengthString(cursor, remaining, "headerValue", headerValue);
    if (headerValueRes.hasError()) {
      return headerValueRes;
    }
    parsed += *headerValueRes;
    remaining -= *headerValueRes;

    headers->set(headerName, headerValue);
    numHeaders++;
  }
  if (numHeaders < 1 && !isTrailers) {
    return folly::makeUnexpected(
        fmt::format("Number of headers (key value pairs) should be >= 1. "
                    "Header count is {}",
                    numHeaders));
  }

  return parsed;
}

ParseResult HTTPBinaryCodec::parseHeaders(folly::io::Cursor& cursor,
                                          size_t remaining,
                                          HTTPMessage& msg) {
  return parseHeadersHelper(cursor, remaining, msg, false);
}

ParseResult HTTPBinaryCodec::parseContent(folly::io::Cursor& cursor,
                                          size_t remaining,
                                          HTTPMessage& msg) {
  size_t parsed = 0;

  // Parse the contentLength and advance cursor
  auto contentLength = quic::decodeQuicInteger(cursor);
  if (!contentLength) {
    return folly::makeUnexpected(
        std::string("Failure to parse content length"));
  }
  // Increase parsed by the number of bytes read
  parsed += contentLength->second;
  // Check that we have not gone beyond "remaining"
  if (contentLength->first > remaining - parsed) {
    return folly::makeUnexpected(std::string("Failure to parse content"));
  }

  // Write the data to msgBody_ and then advance the cursor
  msgBody_ = std::make_unique<folly::IOBuf>();
  cursor.clone(*msgBody_.get(), contentLength->first);

  // Increase parsed by the number of bytes read
  parsed += contentLength->first;
  return parsed;
}

ParseResult HTTPBinaryCodec::parseTrailers(folly::io::Cursor& cursor,
                                           size_t remaining,
                                           HTTPMessage& msg) {
  return parseHeadersHelper(cursor, remaining, msg, true);
}

size_t HTTPBinaryCodec::onIngress(const folly::IOBuf& buf) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return 0;
}

void HTTPBinaryCodec::onIngressEOF() {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return;
}

void HTTPBinaryCodec::generateHeader(
    folly::IOBufQueue& writeBuf,
    StreamID txn,
    const HTTPMessage& msg,
    bool eom,
    HTTPHeaderSize* size,
    const folly::Optional<HTTPHeaders>& extraHeaders) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return;
}

size_t HTTPBinaryCodec::generateBody(folly::IOBufQueue& writeBuf,
                                     StreamID txn,
                                     std::unique_ptr<folly::IOBuf> chain,
                                     folly::Optional<uint8_t> padding,
                                     bool eom) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return 0;
}

size_t HTTPBinaryCodec::generateTrailers(folly::IOBufQueue& writeBuf,
                                         StreamID txn,
                                         const HTTPHeaders& trailers) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return 0;
}

size_t HTTPBinaryCodec::generateEOM(folly::IOBufQueue& writeBuf, StreamID txn) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return 0;
}

size_t HTTPBinaryCodec::generateChunkHeader(folly::IOBufQueue& writeBuf,
                                            HTTPBinaryCodec::StreamID stream,
                                            size_t length) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return 0;
}

size_t HTTPBinaryCodec::generateChunkTerminator(
    folly::IOBufQueue& writeBuf, HTTPBinaryCodec::StreamID stream) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return 0;
}

size_t HTTPBinaryCodec::generateRstStream(folly::IOBufQueue& writeBuf,
                                          HTTPBinaryCodec::StreamID stream,
                                          ErrorCode statusCode) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return 0;
}

size_t HTTPBinaryCodec::generateGoaway(
    folly::IOBufQueue& writeBuf,
    HTTPBinaryCodec::StreamID lastStream,
    ErrorCode statusCode,
    std::unique_ptr<folly::IOBuf> debugData) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return 0;
}

} // namespace proxygen
