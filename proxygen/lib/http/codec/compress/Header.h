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

#include <proxygen/lib/http/HTTPHeaders.h>
#include <string>

namespace proxygen { namespace compress {

/**
 * Helper structure used when serializing the uncompressed
 * representation of a header name/value list.
 */
struct Header {
  HTTPHeaderCode code;
  const std::string* name;
  const std::string* value;

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

  // For use by tests
  static Header makeHeaderForTest(const std::string& n, const std::string& v) {
    return Header(n, v);
  }

 private:
  // This constructor ideally should not be used in production code
  // This is because in prod the common header code is likely already known and
  // an above constructor could be used; this exists for test purposes
  Header(const std::string& n, const std::string& v)
    : code(HTTPCommonHeaders::hash(n)), name(&n), value(&v) {}
};

}}
