/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <proxygen/lib/http/StatusTypeEnum.h>
#include <sstream>
#include <string>

namespace proxygen {

/**
 * Proxy status returned as a header in HTTP responses.
 *   https://tools.ietf.org/html/draft-ietf-httpbis-proxy-status-00
 */
class ProxyStatus {
 public:
  explicit ProxyStatus(StatusType statusType) : statusType_{statusType} {
  }
  StatusType getStatusType() const {
    return statusType_;
  }
  const std::string& getUpstreamIP() const {
    return upstreamIP_;
  };
  ProxyStatus& setProxy(const std::string& proxy) {
    proxy_ = proxy;
    return *this;
  }
  ProxyStatus& setUpstreamIP(const std::string& upstreamIP) {
    upstreamIP_ = upstreamIP;
    return *this;
  }
  std::string str() const {
    std::stringstream s;
    s << getStatusTypeString(statusType_);
    if (!proxy_.empty()) {
      s << "; proxy=" << proxy_;
    }
    if (!upstreamIP_.empty()) {
      s << "; upstream_ip=" << upstreamIP_;
    }
    return s.str();
  }

 protected:
  StatusType statusType_;
  std::string proxy_;
  std::string upstreamIP_;
};

} // namespace proxygen
