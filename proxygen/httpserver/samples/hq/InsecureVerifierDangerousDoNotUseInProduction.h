/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fizz/protocol/CertificateVerifier.h>

namespace proxygen {

// This is an insecure certificate verifier and is not meant to be
// used in production. Using it in production would mean that this will
// leave everyone insecure.
class InsecureVerifierDangerousDoNotUseInProduction
    : public fizz::CertificateVerifier {
 public:
  ~InsecureVerifierDangerousDoNotUseInProduction() override = default;

  void verify(const std::vector<std::shared_ptr<const fizz::PeerCert>>&)
      const override {
    return;
  }

  std::vector<fizz::Extension> getCertificateRequestExtensions()
      const override {
    return std::vector<fizz::Extension>();
  }
};
} // namespace proxygen
