/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/HQUtils.h>
#include <folly/Overload.h>

namespace proxygen { namespace hq {

const quic::StreamId kSessionStreamId = std::numeric_limits<uint64_t>::max();

// Ingress Sett.hngs Default Values
const uint64_t kDefaultIngressHeaderTableSize = 0;
const uint64_t kDefaultIngressNumPlaceHolders = 0;
const uint64_t kDefaultIngressMaxHeaderListSize = 1u << 17;
const uint64_t kDefaultIngressQpackBlockedStream = 0;

// Egress Settings Default Values
const uint64_t kDefaultEgressHeaderTableSize = 4096;
const uint64_t kDefaultEgressNumPlaceHolders = 16;
const uint64_t kDefaultEgressMaxHeaderListSize = 1u << 17;
const uint64_t kDefaultEgressQpackBlockedStream = 100;

proxygen::ErrorCode hqToHttpErrorCode(HTTP3::ErrorCode err) {
  switch (err) {
    case HTTP3::ErrorCode::HTTP_NO_ERROR:
      return ErrorCode::NO_ERROR;
    case HTTP3::ErrorCode::HTTP_PUSH_REFUSED:
      return ErrorCode::REFUSED_STREAM;
    case HTTP3::ErrorCode::HTTP_INTERNAL_ERROR:
      return ErrorCode::INTERNAL_ERROR;
    case HTTP3::ErrorCode::HTTP_PUSH_ALREADY_IN_CACHE:
      return ErrorCode::REFUSED_STREAM;
    case HTTP3::ErrorCode::HTTP_REQUEST_CANCELLED:
      return ErrorCode::CANCEL;
    case HTTP3::ErrorCode::HTTP_INCOMPLETE_REQUEST:
      return ErrorCode::PROTOCOL_ERROR;
    case HTTP3::ErrorCode::HTTP_CONNECT_ERROR:
      return ErrorCode::CONNECT_ERROR;
    case HTTP3::ErrorCode::HTTP_EXCESSIVE_LOAD:
      return ErrorCode::ENHANCE_YOUR_CALM;
    case HTTP3::ErrorCode::HTTP_VERSION_FALLBACK:
      return ErrorCode::INTERNAL_ERROR;
    case HTTP3::ErrorCode::HTTP_WRONG_STREAM:
    case HTTP3::ErrorCode::HTTP_PUSH_LIMIT_EXCEEDED:
    case HTTP3::ErrorCode::HTTP_DUPLICATE_PUSH:
    case HTTP3::ErrorCode::HTTP_UNKNOWN_STREAM_TYPE:
    case HTTP3::ErrorCode::HTTP_WRONG_STREAM_COUNT:
    case HTTP3::ErrorCode::HTTP_CLOSED_CRITICAL_STREAM:
    case HTTP3::ErrorCode::HTTP_WRONG_STREAM_DIRECTION:
    case HTTP3::ErrorCode::HTTP_EARLY_RESPONSE:
    case HTTP3::ErrorCode::HTTP_MISSING_SETTINGS:
    case HTTP3::ErrorCode::HTTP_UNEXPECTED_FRAME:
      return ErrorCode::PROTOCOL_ERROR;
    case HTTP3::ErrorCode::HTTP_REQUEST_REJECTED:
      // Not sure this makes sense...
      return ErrorCode::CANCEL;
    case HTTP3::ErrorCode::HTTP_MALFORMED_FRAME:
    case HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_DATA:
    case HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_HEADERS:
    case HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PRIORITY:
    case HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_CANCEL_PUSH:
    case HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_SETTINGS:
    case HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_PUSH_PROMISE:
    case HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_GOAWAY:
    case HTTP3::ErrorCode::HTTP_MALFORMED_FRAME_MAX_PUSH_ID:
      return ErrorCode::PROTOCOL_ERROR;
    default:
      return ErrorCode::INTERNAL_ERROR;
  }
}

HTTP3::ErrorCode toHTTP3ErrorCode(proxygen::ErrorCode err) {
  switch (err) {
    case ErrorCode::NO_ERROR:
      return HTTP3::ErrorCode::HTTP_NO_ERROR;
    case ErrorCode::PROTOCOL_ERROR:
      return HTTP3::ErrorCode::HTTP_GENERAL_PROTOCOL_ERROR;
    case ErrorCode::INTERNAL_ERROR:
      return HTTP3::ErrorCode::HTTP_INTERNAL_ERROR;
    case ErrorCode::FLOW_CONTROL_ERROR:
      DCHECK(false) << "ErrorCode::FLOW_CONTROL_ERROR for QUIC";
      // fallthrough
    case ErrorCode::SETTINGS_TIMEOUT: // maybe we should keep this?
    case ErrorCode::STREAM_CLOSED:
      return HTTP3::ErrorCode::HTTP_GENERAL_PROTOCOL_ERROR;
    case ErrorCode::FRAME_SIZE_ERROR:
      return HTTP3::ErrorCode::HTTP_MALFORMED_FRAME;
    case ErrorCode::REFUSED_STREAM:
      return HTTP3::ErrorCode::HTTP_PUSH_REFUSED;
    case ErrorCode::CANCEL:
      return HTTP3::ErrorCode::HTTP_REQUEST_CANCELLED;
    case ErrorCode::COMPRESSION_ERROR:
      return HTTP3::ErrorCode::HTTP_QPACK_DECOMPRESSION_FAILED;
    case ErrorCode::CONNECT_ERROR:
      return HTTP3::ErrorCode::HTTP_CONNECT_ERROR;
    case ErrorCode::ENHANCE_YOUR_CALM:
      return HTTP3::ErrorCode::HTTP_EXCESSIVE_LOAD;
    case ErrorCode::INADEQUATE_SECURITY:
    case ErrorCode::HTTP_1_1_REQUIRED:
    default:
      return HTTP3::ErrorCode::HTTP_GENERAL_PROTOCOL_ERROR;
  }
}

HTTP3::ErrorCode toHTTP3ErrorCode(const HTTPException& ex) {
  // TODO: when quic is OSS, add HTTP3ErrorCode to HTTPException
  if (ex.hasHttpStatusCode()) {
    return HTTP3::ErrorCode::HTTP_NO_ERROR; // does this sound right?
  } else if (ex.hasCodecStatusCode()) {
    return toHTTP3ErrorCode(ex.getCodecStatusCode());
  } else if (ex.hasErrno()) {
    return static_cast<HTTP3::ErrorCode>(ex.getErrno());
  }
  return HTTP3::ErrorCode::HTTP_GENERAL_PROTOCOL_ERROR;
}

ProxygenError toProxygenError(quic::QuicErrorCode error, bool fromPeer) {
  switch (error.type()) {
    case quic::QuicErrorCode::Type::ApplicationErrorCode_E:
      return fromPeer ? kErrorConnectionReset : kErrorConnection;
    case quic::QuicErrorCode::Type::LocalErrorCode_E:
      return kErrorShutdown;
    case quic::QuicErrorCode::Type::TransportErrorCode_E:
      return kErrorConnectionReset;
  }
  folly::assume_unreachable();
}

folly::Optional<hq::SettingId> httpToHqSettingsId(proxygen::SettingsId id) {
  switch (id) {
    case proxygen::SettingsId::HEADER_TABLE_SIZE:
      return hq::SettingId::HEADER_TABLE_SIZE;
    case proxygen::SettingsId::MAX_HEADER_LIST_SIZE:
      return hq::SettingId::MAX_HEADER_LIST_SIZE;
    case proxygen::SettingsId::_HQ_QPACK_BLOCKED_STREAMS:
      return hq::SettingId::QPACK_BLOCKED_STREAMS;
    default:
      return folly::none; // this setting has no meaning in HQ
  }
  return folly::none;
}

folly::Optional<proxygen::SettingsId> hqToHttpSettingsId(hq::SettingId id) {
  switch (id) {
    case hq::SettingId::HEADER_TABLE_SIZE:
      return proxygen::SettingsId::HEADER_TABLE_SIZE;
    case hq::SettingId::MAX_HEADER_LIST_SIZE:
      return proxygen::SettingsId::MAX_HEADER_LIST_SIZE;
    case hq::SettingId::QPACK_BLOCKED_STREAMS:
      return proxygen::SettingsId::_HQ_QPACK_BLOCKED_STREAMS;
  }
  return folly::none;
}

}} // namespace proxygen::hq
