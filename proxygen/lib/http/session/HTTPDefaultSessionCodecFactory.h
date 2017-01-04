/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/HTTPCodecFactory.h>
#include <proxygen/lib/http/codec/SPDYVersion.h>
#include <proxygen/lib/services/AcceptorConfiguration.h>

namespace proxygen {

class HTTPDefaultSessionCodecFactory : public HTTPCodecFactory {
 public:
  explicit HTTPDefaultSessionCodecFactory(
      const AcceptorConfiguration& accConfig);
  ~HTTPDefaultSessionCodecFactory() override {}

  /**
   * Get a codec instance
   */
  std::unique_ptr<HTTPCodec> getCodec(const std::string& nextProtocol,
                                      TransportDirection direction) override;

 protected:
  const AcceptorConfiguration& accConfig_;
  bool isSSL_;
  folly::Optional<SPDYVersion> alwaysUseSPDYVersion_{};
  folly::Optional<bool> alwaysUseHTTP2_{};
};

} // proxygen
