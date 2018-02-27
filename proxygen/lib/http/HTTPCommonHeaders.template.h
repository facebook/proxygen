/*
 *  Copyright (c) 2015-present, Facebook, Inc.
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

const uint8_t HTTPHeaderCodeCommonOffset = 2;

enum HTTPCommonHeaderTableType: uint8_t {
  TABLE_CAMELCASE = 0,
  TABLE_LOWERCASE = 1,
};

class HTTPCommonHeaders {
 public:
  // Perfect hash function to match common HTTP header names
  FB_EXPORT static HTTPHeaderCode hash(const char* name, size_t len);

  FB_EXPORT inline static HTTPHeaderCode hash(const std::string& name) {
    return hash(name.data(), name.length());
  }

  FB_EXPORT static std::string* initHeaderNames(HTTPCommonHeaderTableType type);
$$$$$

  static const std::string* getPointerToCommonHeaderTable(
    HTTPCommonHeaderTableType type);

  inline static const std::string* getPointerToHeaderName(HTTPHeaderCode code,
      HTTPCommonHeaderTableType type = TABLE_CAMELCASE) {
    return getPointerToCommonHeaderTable(type) + code;
  }

  inline static bool isHeaderNameFromTable(const std::string* headerName,
      HTTPCommonHeaderTableType type) {
    return getHeaderCodeFromTableCommonHeaderName(headerName, type) >=
      HTTPHeaderCodeCommonOffset;
  }

  // This method supplements hash().  If dealing with string pointers, some
  // pointing to entries in the the common header name table and some not, this
  // method can be used in place of hash to reverse map a string from the common
  // header name table to its HTTPHeaderCode
  inline static HTTPHeaderCode getHeaderCodeFromTableCommonHeaderName(
      const std::string* headerName, HTTPCommonHeaderTableType type) {
    if (headerName == nullptr) {
      return HTTP_HEADER_NONE;
    } else {
      auto diff = headerName - getPointerToCommonHeaderTable(type);
      if (diff >= HTTPHeaderCodeCommonOffset && diff < (long)num_header_codes) {
        return static_cast<HTTPHeaderCode>(diff);
      } else {
        return HTTP_HEADER_OTHER;
      }
    }
  }

};

} // proxygen
