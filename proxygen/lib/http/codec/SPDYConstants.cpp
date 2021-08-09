/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/SPDYConstants.h>

namespace proxygen { namespace spdy {

GoawayStatusCode errorCodeToGoaway(ErrorCode code) {
  switch (code) {
    case ErrorCode::NO_ERROR:
      return GOAWAY_OK;
    case ErrorCode::INTERNAL_ERROR:
      return GOAWAY_INTERNAL_ERROR;
    case ErrorCode::FLOW_CONTROL_ERROR:
      return GOAWAY_FLOW_CONTROL_ERROR;
    case ErrorCode::PROTOCOL_ERROR:
      break;
    case ErrorCode::SETTINGS_TIMEOUT:
      break;
    case ErrorCode::STREAM_CLOSED:
      break;
    case ErrorCode::FRAME_SIZE_ERROR:
      break;
    case ErrorCode::REFUSED_STREAM:
      break;
    case ErrorCode::CANCEL:
      break;
    case ErrorCode::COMPRESSION_ERROR:
      break;
    case ErrorCode::CONNECT_ERROR:
      break;
    case ErrorCode::ENHANCE_YOUR_CALM:
      break;
    case ErrorCode::INADEQUATE_SECURITY:
      break;
    case ErrorCode::HTTP_1_1_REQUIRED:
      break;
  }
  return GOAWAY_PROTOCOL_ERROR;
}

ResetStatusCode errorCodeToReset(ErrorCode code) {
  switch (code) {
    case ErrorCode::NO_ERROR:
      break;
    case ErrorCode::INTERNAL_ERROR:
      return RST_INTERNAL_ERROR;
    case ErrorCode::FLOW_CONTROL_ERROR:
      return RST_FLOW_CONTROL_ERROR;
    case ErrorCode::PROTOCOL_ERROR:
      return RST_PROTOCOL_ERROR;
    case ErrorCode::SETTINGS_TIMEOUT:
      break;
    case ErrorCode::STREAM_CLOSED:
      // Invalid stream comes through here
      return RST_STREAM_ALREADY_CLOSED;
    case ErrorCode::FRAME_SIZE_ERROR:
      return RST_FRAME_TOO_LARGE;
    case ErrorCode::REFUSED_STREAM:
      return RST_REFUSED_STREAM;
    case ErrorCode::CANCEL:
      return RST_CANCEL;
    case ErrorCode::COMPRESSION_ERROR:
      return RST_INTERNAL_ERROR;
    case ErrorCode::CONNECT_ERROR:
      break;
    case ErrorCode::ENHANCE_YOUR_CALM:
      break;
    case ErrorCode::INADEQUATE_SECURITY:
      return RST_INVALID_CREDENTIALS;
    case ErrorCode::HTTP_1_1_REQUIRED:
      break;
  }
  return RST_PROTOCOL_ERROR;
}

ErrorCode goawayToErrorCode(GoawayStatusCode code) {
  switch (code) {
    case GOAWAY_OK:
      return ErrorCode::NO_ERROR;
    case GOAWAY_PROTOCOL_ERROR:
      return ErrorCode::PROTOCOL_ERROR;
    case GOAWAY_INTERNAL_ERROR:
      return ErrorCode::INTERNAL_ERROR;
    case GOAWAY_FLOW_CONTROL_ERROR:
      return ErrorCode::FLOW_CONTROL_ERROR;
  }
  return ErrorCode::PROTOCOL_ERROR;
}

ErrorCode rstToErrorCode(uint32_t code) {
  switch (code) {
    case RST_PROTOCOL_ERROR:
      break;
    case RST_INVALID_STREAM:
      return ErrorCode::STREAM_CLOSED;
    case RST_REFUSED_STREAM:
      return ErrorCode::REFUSED_STREAM;
    case RST_UNSUPPORTED_VERSION:
      break; // not used anyway
    case RST_CANCEL:
      return ErrorCode::CANCEL;
    case RST_INTERNAL_ERROR:
      return ErrorCode::INTERNAL_ERROR;
    case RST_FLOW_CONTROL_ERROR:
      return ErrorCode::FLOW_CONTROL_ERROR;
    case RST_STREAM_IN_USE:
      return ErrorCode::FLOW_CONTROL_ERROR;
    case RST_STREAM_ALREADY_CLOSED:
      return ErrorCode::STREAM_CLOSED;
    case RST_INVALID_CREDENTIALS:
      return ErrorCode::INADEQUATE_SECURITY;
    case RST_FRAME_TOO_LARGE:
      return ErrorCode::FRAME_SIZE_ERROR;
  }
  return ErrorCode::PROTOCOL_ERROR;
}

folly::Optional<proxygen::spdy::SettingsId> httpToSpdySettingsId(
    proxygen::SettingsId id) {
  switch (id) {
    // no mapping
    case proxygen::SettingsId::HEADER_TABLE_SIZE:
    case proxygen::SettingsId::ENABLE_PUSH:
    case proxygen::SettingsId::MAX_FRAME_SIZE:
    case proxygen::SettingsId::MAX_HEADER_LIST_SIZE:
      return folly::none;
    case proxygen::SettingsId::ENABLE_EX_HEADERS:
      return folly::none;
    case proxygen::SettingsId::MAX_CONCURRENT_STREAMS:
      return SETTINGS_MAX_CONCURRENT_STREAMS;
    case proxygen::SettingsId::INITIAL_WINDOW_SIZE:
      return SETTINGS_INITIAL_WINDOW_SIZE;
    case proxygen::SettingsId::_SPDY_UPLOAD_BANDWIDTH:
      return SETTINGS_UPLOAD_BANDWIDTH;
    case proxygen::SettingsId::_SPDY_DOWNLOAD_BANDWIDTH:
      return SETTINGS_DOWNLOAD_BANDWIDTH;
    case proxygen::SettingsId::_SPDY_ROUND_TRIP_TIME:
      return SETTINGS_ROUND_TRIP_TIME;
    case proxygen::SettingsId::_SPDY_CURRENT_CWND:
      return SETTINGS_CURRENT_CWND;
    case proxygen::SettingsId::_SPDY_DOWNLOAD_RETRANS_RATE:
      return SETTINGS_DOWNLOAD_RETRANS_RATE;
    case proxygen::SettingsId::_SPDY_CLIENT_CERTIFICATE_VECTOR_SIZE:
      return SETTINGS_CLIENT_CERTIFICATE_VECTOR_SIZE;
    case proxygen::SettingsId::ENABLE_CONNECT_PROTOCOL:
      return folly::none;
    case proxygen::SettingsId::THRIFT_CHANNEL_ID_DEPRECATED:
    case proxygen::SettingsId::THRIFT_CHANNEL_ID:
      return folly::none;
    case proxygen::SettingsId::_HQ_QPACK_BLOCKED_STREAMS:
    case proxygen::SettingsId::_HQ_DATAGRAM:
    case proxygen::SettingsId::SETTINGS_HTTP_CERT_AUTH:
      return folly::none;
  }
  return folly::none;
}

folly::Optional<proxygen::SettingsId> spdyToHttpSettingsId(
    proxygen::spdy::SettingsId id) {
  switch (id) {
    case SETTINGS_UPLOAD_BANDWIDTH:
    case SETTINGS_DOWNLOAD_BANDWIDTH:
    case SETTINGS_ROUND_TRIP_TIME:
    case SETTINGS_CURRENT_CWND:
    case SETTINGS_DOWNLOAD_RETRANS_RATE:
    case SETTINGS_CLIENT_CERTIFICATE_VECTOR_SIZE:
      // These mappings are possible, but not needed right now
      return folly::none;
    case SETTINGS_MAX_CONCURRENT_STREAMS:
      return proxygen::SettingsId::MAX_CONCURRENT_STREAMS;
    case SETTINGS_INITIAL_WINDOW_SIZE:
      return proxygen::SettingsId::INITIAL_WINDOW_SIZE;
  }
  return folly::none;
}

const uint32_t kInitialWindow = 65536;
const uint32_t kMaxConcurrentStreams = 100;
const uint32_t kMaxFrameLength = (1 << 24) - 1;

const std::string kSessionProtoNameSPDY3("spdy/3");

const std::string httpVersion("HTTP/1.1");
const std::string kNameVersionv2("version");
const std::string kNameVersionv3(":version");
const std::string kNameStatusv2("status");
const std::string kNameStatusv3(":status");
const std::string kNameMethodv2("method");
const std::string kNameMethodv3(":method");
const std::string kNamePathv2("url");
const std::string kNamePathv3(":path");
const std::string kNameSchemev2("scheme");
const std::string kNameSchemev3(":scheme");
const std::string kNameHostv3(":host"); // SPDY v3 only

const std::string kVersionStrv2("spdy/2");
const std::string kVersionStrv3("spdy/3");
const std::string kVersionStrv31("spdy/3.1");

// In the future, we may be shifting the SPDY wire priority
// by this much so we can easily use the lower bits to do our
// own priority queueing within the bands defined by the SPDY
// protocol...
//
// so far:
//
// lower 2 LSB: used to randomly approximate some fairness within
// priority bands, relying on the poisson events of extracting or
// appending a frame to gather "entropy".
const size_t SPDY_PRIO_SHIFT_FACTOR = 2; // up to 60

}} // namespace proxygen::spdy
