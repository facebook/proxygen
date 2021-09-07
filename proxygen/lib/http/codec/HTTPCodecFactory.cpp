/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/HTTPCodecFactory.h>

#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/HTTP2Codec.h>
#include <proxygen/lib/http/codec/SPDYCodec.h>

namespace proxygen {

std::unique_ptr<HTTPCodec> HTTPCodecFactory::getCodec(
    CodecProtocol protocol,
    TransportDirection direction,
    bool strictValidation) {
  // Static m
  switch (protocol) {
    case CodecProtocol::HTTP_1_1:
      return std::make_unique<HTTP1xCodec>(
          direction, /*force_1_1=*/false, strictValidation);
    case CodecProtocol::SPDY_3:
      return std::make_unique<SPDYCodec>(direction, SPDYVersion::SPDY3);
    case CodecProtocol::SPDY_3_1:
      return std::make_unique<SPDYCodec>(direction, SPDYVersion::SPDY3_1);
    case CodecProtocol::HTTP_2: {
      auto codec = std::make_unique<HTTP2Codec>(direction);
      codec->setStrictValidation(strictValidation);
      return codec;
    }
    default:
      LOG(FATAL) << "Unreachable";
      return nullptr;
  }
}

} // namespace proxygen
