/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cstdint>
#include <folly/Optional.h>
#include <proxygen/lib/utils/Export.h>
#include <string>

namespace proxygen {

enum class CodecProtocol : uint8_t {
  HTTP_1_1,
  SPDY_3,
  SPDY_3_1,
  HTTP_2,
  HQ,
};

/**
 * Returns a debugging name to refer to the given protocol.
 */
extern const std::string& getCodecProtocolString(CodecProtocol);

/**
 * Check if given debugging name refers to a valid protocol.
 */
extern bool isValidCodecProtocolStr(const std::string& protocolStr);

/**
 * Get the protocol from the given debugging name.
 * If it's an invalid string, return the default protocol.
 */
extern CodecProtocol getCodecProtocolFromStr(const std::string& protocolStr);

/**
 * Check if the given protocol is SPDY.
 */
FB_EXPORT extern bool isSpdyCodecProtocol(CodecProtocol protocol);

/**
 * Check if the given protocol is HTTP2.
 */
extern bool isHTTP2CodecProtocol(CodecProtocol protocol);

/**
 * Check if the given protocol is HQ
 */
extern bool isHQCodecProtocol(CodecProtocol protocol);

/**
 * Check if the given protocol supports paraellel requests
 */
extern bool isParallelCodecProtocol(CodecProtocol protocol);

/**
 * Search the client and server protocol lists for a matching native
 * CodecProtocol
 */
extern folly::Optional<std::pair<CodecProtocol, std::string>>
checkForProtocolUpgrade(const std::string& clientUpgrade,
                        const std::string& serverUpgrade,
                        bool serverMode);

}
