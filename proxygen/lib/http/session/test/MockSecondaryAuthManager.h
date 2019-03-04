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

#include <folly/portability/GMock.h>
#include <proxygen/lib/http/session/SecondaryAuthManager.h>

namespace proxygen {

class MockSecondaryAuthManager : public SecondaryAuthManager {
 public:
  MOCK_METHOD2(createAuthRequest,
               std::pair<uint16_t, std::unique_ptr<folly::IOBuf>>(
                   std::shared_ptr<folly::IOBuf>,
                   std::vector<fizz::Extension>&));
  std::pair<uint16_t, std::unique_ptr<folly::IOBuf>> createAuthRequest(
      std::unique_ptr<folly::IOBuf> certRequestContext,
      std::vector<fizz::Extension> extensions) override {
    return createAuthRequest(
        std::shared_ptr<folly::IOBuf>(certRequestContext.release()),
        extensions);
  }
  MOCK_METHOD4(getAuthenticator,
               std::pair<uint16_t, std::unique_ptr<folly::IOBuf>>(
                   const fizz::AsyncFizzBase&,
                   TransportDirection,
                   uint16_t,
                   std::shared_ptr<folly::IOBuf>));
  std::pair<uint16_t, std::unique_ptr<folly::IOBuf>> getAuthenticator(
      const fizz::AsyncFizzBase& transport,
      TransportDirection dir,
      uint16_t requestId,
      std::unique_ptr<folly::IOBuf> authRequest) override {
    return getAuthenticator(
        transport,
        dir,
        requestId,
        std::shared_ptr<folly::IOBuf>(authRequest.release()));
  }
  MOCK_METHOD4(validateAuthenticator,
               bool(const fizz::AsyncFizzBase&,
                    TransportDirection,
                    uint16_t,
                    std::shared_ptr<folly::IOBuf>));
  bool validateAuthenticator(
      const fizz::AsyncFizzBase& transport,
      TransportDirection dir,
      uint16_t certId,
      std::unique_ptr<folly::IOBuf> authenticator) override {
    return validateAuthenticator(
        transport,
        dir,
        certId,
        std::shared_ptr<folly::IOBuf>(authenticator.release()));
  }
};
} // namespace proxygen
