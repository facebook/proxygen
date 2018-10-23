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

#include <fizz/extensions/exportedauth/ExportedAuthenticator.h>
#include <fizz/protocol/Certificate.h>

namespace proxygen {

class SecondaryAuthManagerBase {
 public:

  virtual ~SecondaryAuthManagerBase() = default;

  /**
   * Generate an authenticator request given a certificate_request_context and
   * a set of extensions.
   * @return (request ID, encoded authenticator request)
   */
  virtual std::pair<uint16_t, std::unique_ptr<folly::IOBuf>> createAuthRequest(
      std::unique_ptr<folly::IOBuf> certRequestContext,
      std::vector<fizz::Extension> extensions) = 0;
};

} // namespace proxygen
