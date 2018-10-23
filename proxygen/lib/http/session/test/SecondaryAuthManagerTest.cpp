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
#include <fizz/record/Extensions.h>
#include <fizz/record/Types.h>
#include <folly/Conv.h>
#include <folly/String.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

using namespace proxygen;
using namespace fizz;
using namespace folly;
using namespace folly::io;
using namespace std;

StringPiece expected_auth_request = {
    "120000303132333435363738396162636465660008000d000400020403"};

TEST(SecondaryAuthManagerTest, AuthenticatorRequest) {
  auto certRequestContext = folly::IOBuf::copyBuffer("0123456789abcdef");
  fizz::SignatureAlgorithms sigAlgs;
  sigAlgs.supported_signature_algorithms.push_back(
      SignatureScheme::ecdsa_secp256r1_sha256);
  std::vector<fizz::Extension> extensions;
  extensions.push_back(encodeExtension(std::move(sigAlgs)));
  SecondaryAuthManager authManager;
  auto authRequestPair = authManager.createAuthRequest(
      std::move(certRequestContext), std::move(extensions));
  auto requestId = authRequestPair.first;
  auto authRequest = std::move(authRequestPair.second);
  EXPECT_EQ(requestId, 0);
  EXPECT_EQ(expected_auth_request,
            StringPiece(hexlify(authRequest->coalesce())));
}
