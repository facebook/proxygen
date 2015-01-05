/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/HTTPMethod.h>

#include <folly/Foreach.h>
#include <proxygen/lib/http/HTTPHeaders.h>
#include <vector>

#define HTTP_METHOD_STR(method) #method

namespace {
static const std::vector<std::string> kMethodStrings = {
  HTTP_METHOD_GEN(HTTP_METHOD_STR)
};
}

namespace proxygen {

boost::optional<HTTPMethod> stringToMethod(folly::StringPiece method) {
  FOR_EACH_ENUMERATE(index, cur, kMethodStrings) {
    if (caseInsensitiveEqual(*cur, method)) {
      return HTTPMethod(index);
    }
  }
  return boost::none;
}

const std::string& methodToString(HTTPMethod method) {
  return kMethodStrings[static_cast<unsigned>(method)];
}

std::ostream& operator <<(std::ostream& out, HTTPMethod method) {
  out << methodToString(method);
  return out;
}

}
