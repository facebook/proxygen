/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/HTTPBinaryCodec.h>

namespace proxygen {

HTTPBinaryCodec::HTTPBinaryCodec(TransportDirection direction) {
  transportDirection_ = direction;
  state_ = ParseState::FRAMING_INDICATOR;
}

HTTPBinaryCodec::~HTTPBinaryCodec() {
}

ParseResult HTTPBinaryCodec::parseFramingIndicator(folly::io::Cursor& cursor,
                                                   bool& request,
                                                   bool& knownLength) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return folly::makeUnexpected(std::string("Need to Implement!"));
}

ParseResult HTTPBinaryCodec::parseKnownLengthString(
    folly::io::Cursor& cursor,
    size_t remaining,
    folly::StringPiece stringName,
    std::string& stringValue) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return folly::makeUnexpected(std::string("Need to Implement!"));
}

ParseResult HTTPBinaryCodec::parseRequestControlData(folly::io::Cursor& cursor,
                                                     size_t remaining,
                                                     HTTPMessage& msg) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return folly::makeUnexpected(std::string("Need to Implement!"));
}

ParseResult HTTPBinaryCodec::parseResponseControlData(folly::io::Cursor& cursor,
                                                      size_t remaining,
                                                      HTTPMessage& msg) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return folly::makeUnexpected(std::string("Need to Implement!"));
}

ParseResult HTTPBinaryCodec::parseHeaders(folly::io::Cursor& cursor,
                                          size_t remaining,
                                          HTTPMessage& msg) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return folly::makeUnexpected(std::string("Need to Implement!"));
}

ParseResult HTTPBinaryCodec::parseContent(folly::io::Cursor& cursor,
                                          size_t remaining,
                                          HTTPMessage& msg) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return folly::makeUnexpected(std::string("Need to Implement!"));
}

ParseResult HTTPBinaryCodec::parseTrailers(folly::io::Cursor& cursor,
                                           size_t remaining,
                                           HTTPMessage& msg) {
  // TODO(T118289674) - Implement HTTPBinaryCodec
  return folly::makeUnexpected(std::string("Need to Implement!"));
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
