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
#include <utility>

namespace proxygen {

// Will never be valid HTTP/2 which only has 16 bits
#define SPDY_SETTINGS_MASK (1 << 16)

enum class SettingsId: uint32_t {
  // From HTTP/2
  HEADER_TABLE_SIZE = 1,
  ENABLE_PUSH = 2,
  MAX_CONCURRENT_STREAMS = 3,
  INITIAL_WINDOW_SIZE = 4,
  MAX_FRAME_SIZE = 5,
  MAX_HEADER_LIST_SIZE = 6,

  // From SPDY, mostly unused
  _SPDY_UPLOAD_BANDWIDTH = SPDY_SETTINGS_MASK | 1,
  _SPDY_DOWNLOAD_BANDWIDTH = SPDY_SETTINGS_MASK | 2,
  _SPDY_ROUND_TRIP_TIME = SPDY_SETTINGS_MASK | 3,
  //  MAX_CONCURRENT_STREAMS = 4,
  _SPDY_CURRENT_CWND = SPDY_SETTINGS_MASK | 5,
  _SPDY_DOWNLOAD_RETRANS_RATE = SPDY_SETTINGS_MASK | 6,
  //  INITIAL_WINDOW_SIZE = 7,
  _SPDY_CLIENT_CERTIFICATE_VECTOR_SIZE = SPDY_SETTINGS_MASK  | 8
};

extern const uint8_t kMaxSettingIdentifier;

typedef std::pair<SettingsId, uint32_t> SettingPair;

}
