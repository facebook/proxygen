/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/SecondaryAuthManager.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBufQueue.h>
using namespace fizz;
using folly::io::QueueAppender;

namespace proxygen {

SecondaryAuthManager::SecondaryAuthManager(
    std::unique_ptr<fizz::SelfCert> cert) {
  cert_ = std::move(cert);
}

SecondaryAuthManager::~SecondaryAuthManager() {}

std::pair<uint16_t, std::unique_ptr<folly::IOBuf>>
SecondaryAuthManager::createAuthRequest(
    std::unique_ptr<folly::IOBuf> certRequestContext,
    std::vector<fizz::Extension> extensions) {
  // The certificate_request_context has to include the two octets Request-ID.
  uint16_t requestId = requestIdCounter_++;
  folly::IOBufQueue contextQueue{folly::IOBufQueue::cacheChainLength()};
  auto contextLen =
      sizeof(requestId) + certRequestContext->computeChainDataLength();
  QueueAppender appender(&contextQueue, contextLen);
  appender.writeBE<uint16_t>(requestId);
  contextQueue.append(std::move(certRequestContext));
  auto secureContext = contextQueue.move();
  auto authRequest = fizz::ExportedAuthenticator::getAuthenticatorRequest(
      std::move(secureContext), std::move(extensions));
  auto authRequestClone = authRequest->clone();
  outstandingRequests_.insert(
      std::make_pair(requestId, std::move(authRequest)));
  return std::make_pair(requestId, std::move(authRequestClone));
}

} // namespace proxygen
