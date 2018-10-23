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

#include <proxygen/lib/http/session/SecondaryAuthManagerBase.h>

namespace proxygen {

class SecondaryAuthManager : public SecondaryAuthManagerBase {
 public:
  explicit SecondaryAuthManager(std::unique_ptr<fizz::SelfCert> cert);

  SecondaryAuthManager() = default;

  ~SecondaryAuthManager() override;

  /**
   * Generate an authenticator request given a certificate_request_context and
   * a set of extensions.
   * @return (request ID, encoded authenticator request)
   */
  std::pair<uint16_t, std::unique_ptr<folly::IOBuf>> createAuthRequest(
      std::unique_ptr<folly::IOBuf> certRequestContext,
      std::vector<fizz::Extension> extensions) override;

 private:
  uint16_t requestIdCounter_{0};
  // Locally cached authenticator requests.
  std::map<uint16_t, std::unique_ptr<folly::IOBuf>> outstandingRequests_;

  // Secondary certificate possessed by the local endpoint.
  std::unique_ptr<fizz::SelfCert> cert_;
};

} // namespace proxygen
