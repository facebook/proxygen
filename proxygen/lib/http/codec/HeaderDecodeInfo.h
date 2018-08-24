/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/HTTPRequestVerifier.h>

namespace proxygen {

class HTTPMessage;

class HeaderDecodeInfo {
 public:
  void init(HTTPMessage* msgIn, bool isRequestIn) {
    msg = msgIn;
    isRequest = isRequestIn;
    hasStatus = false;
    hasContentLength = false;
    contentLength = 0;
    regularHeaderSeen = false;
    parsingError = "";
    decodeError = HPACK::DecodeError::NONE;
    verifier.error = "";
    verifier.setMessage(msg);
    verifier.setHasMethod(false);
    verifier.setHasPath(false);
    verifier.setHasScheme(false);
    verifier.setHasAuthority(false);
    verifier.setHasUpgradeProtocol(false);
  }

  bool onHeader(const folly::fbstring& name, const folly::fbstring& value);

  void onHeadersComplete(HTTPHeaderSize decodedSize);

  // Change this to a map of decoded header blocks when we decide
  // to concurrently decode partial header blocks
  HTTPMessage* msg{nullptr};
  HTTPRequestVerifier verifier;
  bool isRequest{false};
  bool hasStatus{false};
  bool regularHeaderSeen{false};
  bool hasContentLength{false};
  uint32_t contentLength{0};
  std::string parsingError;
  HPACK::DecodeError decodeError{HPACK::DecodeError::NONE};
};

}
