/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/HTTPCodecFactory.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/SPDYCodec.h>
#include <proxygen/lib/http/codec/HTTP2Codec.h>

namespace proxygen {

std::unique_ptr<HTTPCodec> HTTPCodecFactory::getCodec(
  CodecProtocol protocol, TransportDirection direction) {
  switch (protocol) {
    case CodecProtocol::HTTP_1_1:
      return folly::make_unique<HTTP1xCodec>(direction);
    case CodecProtocol::SPDY_3:
      return folly::make_unique<SPDYCodec>(direction, SPDYVersion::SPDY3);
    case CodecProtocol::SPDY_3_1:
      return folly::make_unique<SPDYCodec>(direction, SPDYVersion::SPDY3_1);
    case CodecProtocol::HTTP_2:
      return folly::make_unique<HTTP2Codec>(direction);
  }
  LOG(FATAL) << "Unreachable";
  return nullptr;
}

}
