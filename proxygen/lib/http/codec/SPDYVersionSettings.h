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

#include <proxygen/lib/http/codec/SPDYVersion.h>
#include <string>

namespace folly { namespace io {
class Cursor;
}}

namespace proxygen {

/**
 * Helper struct to carry common settings for a spdy version
 */
struct SPDYVersionSettings {
  const std::string versionStr;
  const std::string statusStr;
  const std::string methodStr;
  const std::string pathStr;
  const std::string schemeStr;
  const std::string hostStr;
  const std::string protoName;
  uint32_t (*parseSizeFun)(folly::io::Cursor*);
  void (*appendSizeFun)(uint8_t*&, size_t);
  const unsigned char* dict;
  size_t dictSize;
  uint16_t controlVersion;
  uint16_t synReplySize;
  uint16_t nameValueSize;
  uint16_t goawaySize;
  uint8_t priShift;
  uint8_t majorVersion;
  uint8_t minorVersion;
  SPDYVersion version;
  const std::string& protocolVersionString;
};

}
