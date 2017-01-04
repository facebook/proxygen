/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/HTTPMethod.h>

#include <folly/Foreach.h>
#include <folly/Indestructible.h>
#include <proxygen/lib/http/HTTPHeaders.h>
#include <vector>

#define HTTP_METHOD_STR(method) #method

namespace {

// Method strings. This is indestructible because this structure is
// accessed from multiple threads and still needs to be accessible after exit()
// is called to avoid crashing.
typedef std::vector<std::string> StringVector;

const StringVector& getMethodStrings() {
  static const folly::Indestructible<StringVector> methodStrings{
    StringVector{ HTTP_METHOD_GEN(HTTP_METHOD_STR) }
  };
  return *methodStrings;
}

}

namespace proxygen {

boost::optional<HTTPMethod> stringToMethod(folly::StringPiece method) {
  FOR_EACH_ENUMERATE(index, cur, getMethodStrings()) {
    if (caseInsensitiveEqual(*cur, method)) {
      return HTTPMethod(index);
    }
  }
  return boost::none;
}

const std::string& methodToString(HTTPMethod method) {
  return getMethodStrings()[static_cast<unsigned>(method)];
}

std::ostream& operator <<(std::ostream& out, HTTPMethod method) {
  out << methodToString(method);
  return out;
}

}
