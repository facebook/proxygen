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

#include <cstdint>
#include <string>

#include <proxygen/lib/utils/Export.h>

namespace proxygen {

/**
 * Codes (hashes) of common HTTP header names
 */
enum HTTPHeaderCode : uint8_t {
  // code reserved to indicate the absence of an HTTP header
  HTTP_HEADER_NONE = 0,
  // code for any HTTP header name not in the list of common headers
  HTTP_HEADER_OTHER = 1,

  /* the following is a placeholder for the build script to generate a list
   * of enum values from the list in HTTPCommonHeaders.txt
   *
   * enum name of Some-Header is HTTP_HEADER_SOME_HEADER,
   * so an example fragment of the generated list could be:
   * ...
   * HTTP_HEADER_WARNING = 65,
   * HTTP_HEADER_WWW_AUTHENTICATE = 66,
   * HTTP_HEADER_X_BACKEND = 67,
   * HTTP_HEADER_X_BLOCKID = 68,
   * ...
   */
%%%%%

};

class HTTPCommonHeaders {
 public:
  // Perfect hash function to match common HTTP header names
  FB_EXPORT static HTTPHeaderCode hash(const char* name, size_t len);

  FB_EXPORT inline static HTTPHeaderCode hash(const std::string& name) {
    return hash(name.data(), name.length());
  }

  FB_EXPORT static std::string* initHeaderNames();
$$$$$

  inline static const std::string* getPointerToHeaderName(HTTPHeaderCode code) {
    static const auto headerNames = initHeaderNames();

    return headerNames + code;
  }
};

} // proxygen
