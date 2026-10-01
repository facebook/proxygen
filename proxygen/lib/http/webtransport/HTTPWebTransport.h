/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <folly/Expected.h>
#include <folly/Optional.h>

#include <string>
#include <vector>

namespace proxygen {

class HTTPMessage;

class HTTPWebTransport {
 public:
  enum class WTProtocolError : uint8_t {
    HeaderMissing,
    ParseFailed,
    EmptyList
  };

  static bool isConnectMessage(const proxygen::HTTPMessage& msg);

  static HTTPMessage makeConnectRequest(
      const std::string& authority,
      std::string path,
      const std::vector<std::string>& protocols = {});

  static void setWTAvailableProtocols(
      HTTPMessage& msg, const std::vector<std::string>& protocols);

  static void setWTProtocol(HTTPMessage& msg, std::string protocol);

  static folly::Expected<std::vector<std::string>, WTProtocolError>
  getWTAvailableProtocols(const HTTPMessage& msg);

  static folly::Expected<std::string, WTProtocolError> getWTProtocol(
      const HTTPMessage& msg);

  static folly::Optional<std::string> negotiateWTProtocol(
      const std::vector<std::string>& wtAvailableProtocols,
      const std::vector<std::string>& supportedProtocols);
};

} // namespace proxygen
