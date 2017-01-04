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

#include <cstdint>
#include <string>
#include <proxygen/lib/http/codec/ErrorCode.h>

namespace proxygen { namespace http2 {

extern ErrorCode errorCodeToGoaway(ErrorCode code);
extern ErrorCode errorCodeToReset(ErrorCode code);

extern const uint32_t kFrameHeaderSize;
extern const uint32_t kFrameHeadersBaseMaxSize;
extern const uint32_t kFramePrioritySize;
extern const uint32_t kFrameRstStreamSize;
extern const uint32_t kFramePushPromiseSize;
extern const uint32_t kFramePingSize;
extern const uint32_t kFrameGoawaySize;
extern const uint32_t kFrameWindowUpdateSize;

// These constants indicate the size of the required fields in the frame
extern const uint32_t kFrameAltSvcSizeBase;

extern const uint32_t kMaxFramePayloadLengthMin;
extern const uint32_t kMaxFramePayloadLength;
extern const uint32_t kMaxStreamID;
extern const uint32_t kInitialWindow;
extern const uint32_t kMaxWindowUpdateSize;
extern const uint32_t kMaxHeaderTableSize;

extern const std::string kAuthority;
extern const std::string kMethod;
extern const std::string kPath;
extern const std::string kScheme;
extern const std::string kStatus;

extern const std::string kHttp;
extern const std::string kHttps;

extern const std::string kConnectionPreface;

extern const std::string kProtocolString;
extern const std::string kProtocolDraftString;
extern const std::string kProtocolExperimentalString;
extern const std::string kProtocolCleartextString;
extern const std::string kProtocolSettingsHeader;
}}
