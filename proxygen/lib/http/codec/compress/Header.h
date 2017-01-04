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

#include <proxygen/lib/http/HTTPHeaders.h>
#include <string>

namespace proxygen { namespace compress {

/**
 * Helper structure used when serializing the uncompressed
 * representation of a header name/value list.
 */
struct Header {
  HTTPHeaderCode code{HTTP_HEADER_OTHER};
  const std::string* name;
  const std::string* value;

  Header(const std::string& n,
         const std::string& v)
      : name(&n), value(&v) {}

  Header(HTTPHeaderCode c,
         const std::string& v)
    : code(c), name(HTTPCommonHeaders::getPointerToHeaderName(c)), value(&v) {}

  Header(HTTPHeaderCode c,
         const std::string& n,
         const std::string& v)
    : code(c), name(&n), value(&v) {}

  bool operator<(const Header& h) const {
    return (code < h.code) ||
      ((code == h.code) && (*name < *h.name));
  }
};

}}
