/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace proxygen {

class ProxygenSSL {
 public:
  /**
   * Initialize OpenSSL library and creates SSL app context
   * indices. Call prior to any pool creation.
   */
  static void init();
  static int getSSLAppContextConfigIndex() {
    return sCertAppCtxtConfigIndex_;
  }
  static int getSSLAppContextStatsIndex() {
    return sCertAppCtxtStatsIndex_;
  }
  static int getX509CertNameIndex() {
    return sX509CertNameIndex_;
  }

 private:
  // App context indices within SSL
  static int sCertAppCtxtConfigIndex_;
  static int sCertAppCtxtStatsIndex_;

  // X509 ex index for cert name
  static int sX509CertNameIndex_;

  static void createSSLAppContextIndices();
  static void createX509AppContextIndices();
};

} // namespace proxygen
