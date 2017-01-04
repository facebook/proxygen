/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/CodecProtocol.h>
#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/http/codec/HTTP2Constants.h>
#include <boost/algorithm/string/trim.hpp>

#include <glog/logging.h>

namespace proxygen {

namespace {
static const std::string http_1_1 = "http/1.1";
static const std::string spdy_3 = "spdy/3";
static const std::string spdy_3_1 = "spdy/3.1";
static const std::string http_2 = "http/2";
static const std::string empty = "";

extern CodecProtocol getCodecProtocolFromStr(folly::StringPiece protocolStr) {
  if (protocolStr == http_1_1) {
    return CodecProtocol::HTTP_1_1;
  } else if (protocolStr == spdy_3) {
    return CodecProtocol::SPDY_3;
  } else if (protocolStr == spdy_3_1) {
    return CodecProtocol::SPDY_3_1;
  } else if (protocolStr == http_2 || protocolStr == http2::kProtocolString ||
             protocolStr == http2::kProtocolCleartextString) {
    return CodecProtocol::HTTP_2;
  } else {
    // return default protocol
    return CodecProtocol::HTTP_1_1;
  }
}

}

extern const std::string& getCodecProtocolString(CodecProtocol proto) {
  switch (proto) {
    case CodecProtocol::HTTP_1_1: return http_1_1;
    case CodecProtocol::SPDY_3: return spdy_3;
    case CodecProtocol::SPDY_3_1: return spdy_3_1;
    case CodecProtocol::HTTP_2: return http_2;
  }
  LOG(FATAL) << "Unreachable";
  return empty;
}

extern bool isValidCodecProtocolStr(const std::string& protocolStr) {
  return protocolStr == http_1_1 ||
         protocolStr == spdy_3 ||
         protocolStr == spdy_3_1 ||
         protocolStr == http2::kProtocolString ||
         protocolStr == http2::kProtocolCleartextString ||
         protocolStr == http_2;
}

extern CodecProtocol getCodecProtocolFromStr(const std::string& protocolStr) {
  return getCodecProtocolFromStr(folly::StringPiece(protocolStr));
}

extern bool isSpdyCodecProtocol(CodecProtocol protocol) {
  return protocol == CodecProtocol::SPDY_3 ||
         protocol == CodecProtocol::SPDY_3_1;
}

extern bool isHTTP2CodecProtocol(CodecProtocol protocol) {
  return protocol == CodecProtocol::HTTP_2;
}

extern bool isParallelCodecProtocol(CodecProtocol protocol) {
  return isSpdyCodecProtocol(protocol) || isHTTP2CodecProtocol(protocol);
}

extern boost::optional<std::pair<CodecProtocol, std::string>>
checkForProtocolUpgrade(const std::string& clientUpgrade,
                        const std::string& serverUpgrade,
                        bool serverMode) {
  CodecProtocol protocol;
  if (clientUpgrade.empty() || serverUpgrade.empty()) {
    return boost::none;
  }

  // Should be a comma separated list of protocols, like NPN
  std::vector<folly::StringPiece> clientProtocols;
  folly::split(",", clientUpgrade, clientProtocols, true /* ignore empty */);
  for (auto& clientProtocol: clientProtocols) {
    boost::algorithm::trim(clientProtocol);
  }

  // List of server chosen protocols in layer-ascending order.  We can
  // only support one layer right now.  We just skip anything that
  // isn't an HTTP transport protocol
  std::vector<folly::StringPiece> serverProtocols;
  folly::split(",", serverUpgrade, serverProtocols, true /* ignore empty */);

  for (auto& testProtocol: serverProtocols) {
    // Get rid of leading/trailing LWS
    boost::algorithm::trim(testProtocol);
    if (std::find(clientProtocols.begin(), clientProtocols.end(),
                  testProtocol) == clientProtocols.end()) {
      if (serverMode) {
        // client didn't offer this, try the next
        continue;
      } else {
        // The server returned a protocol the client didn't ask for
        return boost::none;
      }
    }
    protocol = getCodecProtocolFromStr(testProtocol);
    // Non-native upgrades get returned as HTTP_1_1/<actual protocol>
    return std::make_pair(protocol, testProtocol.str());
  }
  return boost::none;
}


const boost::none_t HTTPCodec::NoPadding = boost::none;

}
