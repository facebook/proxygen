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

#include <boost/optional/optional.hpp>
#include <proxygen/lib/http/codec/ErrorCode.h>
#include <proxygen/lib/http/codec/SettingsId.h>
#include <string>

namespace proxygen { namespace spdy {

enum FrameType {
  SYN_STREAM = 1,
  SYN_REPLY = 2,
  RST_STREAM = 3,
  SETTINGS = 4,
  NOOP = 5,
  PING = 6,
  GOAWAY = 7,
  HEADERS = 8,
  WINDOW_UPDATE = 9,
  // The CREDENTIAL frame is removed in SPDY/3.1
  CREDENTIAL = 10
};

enum CtrlFlag {
  CTRL_FLAG_NONE = 0,
  CTRL_FLAG_FIN = 1,
  CTRL_FLAG_UNIDIRECTIONAL = 2
};

enum SettingsFlag {
  FLAG_SETTINGS_NONE = 0,
  FLAG_SETTINGS_CLEAR_SETTINGS = 1
};

enum SettingsIdFlag {
  ID_FLAG_SETTINGS_NONE = 0,
  ID_FLAG_SETTINGS_PERSIST_VALUE = 1,
  ID_FLAG_SETTINGS_PERSISTED = 2
};

enum SettingsId {
  SETTINGS_UPLOAD_BANDWIDTH = 1,
  SETTINGS_DOWNLOAD_BANDWIDTH = 2,
  SETTINGS_ROUND_TRIP_TIME = 3,
  SETTINGS_MAX_CONCURRENT_STREAMS = 4,
  SETTINGS_CURRENT_CWND = 5,
  SETTINGS_DOWNLOAD_RETRANS_RATE = 6,
  SETTINGS_INITIAL_WINDOW_SIZE = 7,
  SETTINGS_CLIENT_CERTIFICATE_VECTOR_SIZE = 8
};

/**
 * The status codes for the RST_STREAM control frame.
 */
enum ResetStatusCode {
  RST_PROTOCOL_ERROR = 1,
  RST_INVALID_STREAM = 2,
  RST_REFUSED_STREAM = 3,
  RST_UNSUPPORTED_VERSION = 4,
  RST_CANCEL = 5,
  RST_INTERNAL_ERROR = 6,
  RST_FLOW_CONTROL_ERROR = 7,
  // The following status codes were added in SPDY/3
  // and are not supported in SPDY/2
  RST_STREAM_IN_USE = 8,
  RST_STREAM_ALREADY_CLOSED = 9,
  RST_INVALID_CREDENTIALS = 10,
  RST_FRAME_TOO_LARGE = 11
};

/**
 * The status codes for the GOAWAY control frame.
 */
enum GoawayStatusCode {
  GOAWAY_OK = 0,
  GOAWAY_PROTOCOL_ERROR = 1,
  GOAWAY_INTERNAL_ERROR = 2,
  // Only for SPDY/3.1 for connection-level flow control errors
  GOAWAY_FLOW_CONTROL_ERROR = 7
};

// Functions

extern GoawayStatusCode errorCodeToGoaway(ErrorCode code);
extern ResetStatusCode errorCodeToReset(ErrorCode code);
extern ErrorCode goawayToErrorCode(spdy::GoawayStatusCode);
extern ErrorCode rstToErrorCode(spdy::ResetStatusCode);

boost::optional<proxygen::spdy::SettingsId> httpToSpdySettingsId(
  proxygen::SettingsId id);
boost::optional<proxygen::SettingsId> spdyToHttpSettingsId(
  proxygen::spdy::SettingsId id);

// Constants

extern const uint32_t kInitialWindow;
extern const uint32_t kMaxConcurrentStreams;
extern const uint32_t kMaxFrameLength;

extern const std::string kSessionProtoNameSPDY2;
extern const std::string kSessionProtoNameSPDY3;

extern const std::string httpVersion;
extern const std::string kNameVersionv2;
extern const std::string kNameVersionv3;
extern const std::string kNameStatusv2;
extern const std::string kNameStatusv3;
extern const std::string kNameMethodv2;
extern const std::string kNameMethodv3;
extern const std::string kNamePathv2;
extern const std::string kNamePathv3;
extern const std::string kNameSchemev2;
extern const std::string kNameSchemev3;
extern const std::string kNameHostv3;

extern const std::string kVersionStrv2;
extern const std::string kVersionStrv3;
extern const std::string kVersionStrv31;

extern const size_t SPDY_PRIO_SHIFT_FACTOR;

}}
