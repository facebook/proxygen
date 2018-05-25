/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPDefaultSessionCodecFactory.h>

#include <proxygen/lib/http/codec/HTTP2Constants.h>
#include <proxygen/lib/http/codec/HTTP2Codec.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/SPDYCodec.h>

namespace proxygen {

HTTPDefaultSessionCodecFactory::HTTPDefaultSessionCodecFactory(
    const AcceptorConfiguration& accConfig)
    : accConfig_(accConfig) {
  // set up codec defaults in the case of plaintext connections
  auto version = SPDYCodec::getVersion(accConfig.plaintextProtocol);
  if (version) {
    alwaysUseSPDYVersion_ = *version;
  } else if (accConfig.plaintextProtocol == http2::kProtocolCleartextString) {
    alwaysUseHTTP2_ = true;
  }
}

std::unique_ptr<HTTPCodec> HTTPDefaultSessionCodecFactory::getCodec(
    const std::string& nextProtocol, TransportDirection direction, bool isTLS) {
  if (!isTLS && alwaysUseSPDYVersion_) {
    return std::make_unique<SPDYCodec>(direction,
                                       alwaysUseSPDYVersion_.value(),
                                       accConfig_.spdyCompressionLevel);
  } else if (!isTLS && alwaysUseHTTP2_) {
    return std::make_unique<HTTP2Codec>(direction);
  } else if (nextProtocol.empty() ||
             HTTP1xCodec::supportsNextProtocol(nextProtocol)) {
    auto codec = std::make_unique<HTTP1xCodec>(direction);
    if (!isTLS) {
      codec->setAllowedUpgradeProtocols(
        accConfig_.allowedPlaintextUpgradeProtocols);
    }
    return std::move(codec);
  } else if (auto version = SPDYCodec::getVersion(nextProtocol)) {
    return std::make_unique<SPDYCodec>(direction, *version,
                                         accConfig_.spdyCompressionLevel);
  } else if (nextProtocol == http2::kProtocolString ||
             nextProtocol == http2::kProtocolDraftString ||
             nextProtocol == http2::kProtocolExperimentalString) {
    return std::make_unique<HTTP2Codec>(direction);
  } else {
    VLOG(2) << "Client requested unrecognized next protocol " << nextProtocol;
  }

  return nullptr;
}
}
