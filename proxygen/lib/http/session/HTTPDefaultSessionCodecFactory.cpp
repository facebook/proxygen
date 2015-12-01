/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPDefaultSessionCodecFactory.h>

#include <proxygen/lib/http/codec/experimental/HTTP2Constants.h>
#include <proxygen/lib/http/codec/experimental/HTTP2Codec.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/SPDYCodec.h>

namespace proxygen {

HTTPDefaultSessionCodecFactory::HTTPDefaultSessionCodecFactory(
    const AcceptorConfiguration& accConfig)
    : accConfig_(accConfig), isSSL_(accConfig.isSSL()) {
  if (!isSSL_) {
    auto version = SPDYCodec::getVersion(accConfig.plaintextProtocol);
    if (version) {
      alwaysUseSPDYVersion_ = *version;
    } else if (accConfig.plaintextProtocol == http2::kProtocolCleartextString) {
      alwaysUseHTTP2_ = true;
    }
  }
}

std::unique_ptr<HTTPCodec> HTTPDefaultSessionCodecFactory::getCodec(
    const std::string& nextProtocol, TransportDirection direction) {
  if (!isSSL_ && alwaysUseSPDYVersion_) {
    return folly::make_unique<SPDYCodec>(TransportDirection::DOWNSTREAM,
                                         alwaysUseSPDYVersion_.value(),
                                         accConfig_.spdyCompressionLevel);
  } else if (!isSSL_ && alwaysUseHTTP2_) {
    return folly::make_unique<HTTP2Codec>(TransportDirection::DOWNSTREAM);
  } else if (nextProtocol.empty() ||
             HTTP1xCodec::supportsNextProtocol(nextProtocol)) {
    return folly::make_unique<HTTP1xCodec>(TransportDirection::DOWNSTREAM);
  } else if (auto version = SPDYCodec::getVersion(nextProtocol)) {
    return folly::make_unique<SPDYCodec>(TransportDirection::DOWNSTREAM,
                                         *version,
                                         accConfig_.spdyCompressionLevel);
  } else if (nextProtocol == http2::kProtocolString || nextProtocol == "h2") {
    return folly::make_unique<HTTP2Codec>(TransportDirection::DOWNSTREAM);
  } else {
    VLOG(2) << "Client requested unrecognized next protocol " << nextProtocol;
  }

  return nullptr;
}
}
