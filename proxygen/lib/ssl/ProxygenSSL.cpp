/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "proxygen/lib/ssl/ProxygenSSL.h"

#include <folly/portability/OpenSSL.h>
#include <folly/ssl/Init.h>
#include <glog/logging.h>

namespace proxygen {

int ProxygenSSL::sX509CertNameIndex_ = -1;
int ProxygenSSL::sCertAppCtxtConfigIndex_ = 0;
int ProxygenSSL::sCertAppCtxtStatsIndex_ = 1;

/* static */ void ProxygenSSL::init() {
  folly::ssl::init();

  createSSLAppContextIndices();
  createX509AppContextIndices();
}

/* static */ void ProxygenSSL::createSSLAppContextIndices() {
  CHECK_EQ(sCertAppCtxtConfigIndex_, 0);
  sCertAppCtxtConfigIndex_ = SSL_CTX_get_ex_new_index(
      0, (void*)"proxygen certverificationdata", nullptr, nullptr, nullptr);

  CHECK_EQ(sCertAppCtxtStatsIndex_, 1);
  sCertAppCtxtStatsIndex_ = SSL_CTX_get_ex_new_index(
      0, (void*)"proxygen certverificationdata", nullptr, nullptr, nullptr);
}

/* static */ void ProxygenSSL::createX509AppContextIndices() {
  CHECK_EQ(sX509CertNameIndex_, -1);
  sX509CertNameIndex_ = X509_get_ex_new_index(
      0, (void*)"x509 index for certificate name", nullptr, nullptr, nullptr);
}

} // namespace proxygen
