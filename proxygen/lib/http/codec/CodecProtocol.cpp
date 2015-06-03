/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/CodecProtocol.h>
#include <proxygen/lib/http/codec/HTTPCodec.h>

#include <glog/logging.h>

namespace proxygen {

namespace {
static const std::string http_1_1 = "http/1.1";
static const std::string spdy_2 = "spdy/2";
static const std::string spdy_3 = "spdy/3";
static const std::string spdy_3_1 = "spdy/3.1";
static const std::string spdy_3_1_hpack = "spdy/3.1-hpack";
static const std::string http_2 = "http/2";
static const std::string empty = "";
}

extern const std::string& getCodecProtocolString(CodecProtocol proto) {
  switch (proto) {
    case CodecProtocol::HTTP_1_1: return http_1_1;
    case CodecProtocol::SPDY_2: return spdy_2;
    case CodecProtocol::SPDY_3: return spdy_3;
    case CodecProtocol::SPDY_3_1: return spdy_3_1;
    case CodecProtocol::SPDY_3_1_HPACK: return spdy_3_1_hpack;
    case CodecProtocol::HTTP_2: return http_2;
  }
  LOG(FATAL) << "Unreachable";
  return empty;
}

extern bool isValidCodecProtocolStr(const std::string& protocolStr) {
  return protocolStr == http_1_1 ||
         protocolStr == spdy_2 ||
         protocolStr == spdy_3 ||
         protocolStr == spdy_3_1 ||
         protocolStr == spdy_3_1_hpack ||
         protocolStr == http_2;
}

extern CodecProtocol getCodecProtocolFromStr(const std::string& protocolStr) {
  if (protocolStr == http_1_1) {
    return CodecProtocol::HTTP_1_1;
  } else if (protocolStr == spdy_2) {
    return CodecProtocol::SPDY_2;
  } else if (protocolStr == spdy_3) {
    return CodecProtocol::SPDY_3;
  } else if (protocolStr == spdy_3_1) {
    return CodecProtocol::SPDY_3_1;
  } else if (protocolStr == spdy_3_1_hpack) {
    return CodecProtocol::SPDY_3_1_HPACK;
  } else if (protocolStr == http_2) {
    return CodecProtocol::HTTP_2;
  } else {
    // return default protocol
    return CodecProtocol::HTTP_1_1;
  }
}

extern bool isSpdyCodecProtocol(CodecProtocol protocol) {
  return protocol == CodecProtocol::SPDY_2 ||
         protocol == CodecProtocol::SPDY_3 ||
         protocol == CodecProtocol::SPDY_3_1 ||
         protocol == CodecProtocol::SPDY_3_1_HPACK;
}

const boost::none_t HTTPCodec::NoPadding = boost::none;

}
