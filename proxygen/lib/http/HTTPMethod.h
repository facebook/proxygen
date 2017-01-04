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

#include <boost/optional/optional.hpp>
#include <folly/Range.h>
#include <string>

namespace proxygen {

// Ordered by frequency to minimize time spent in iteration
#define HTTP_METHOD_GEN(x) \
  x(GET),                  \
  x(POST),                 \
  x(OPTIONS),              \
  x(DELETE),               \
  x(HEAD),                 \
  x(CONNECT),              \
  x(PUT),                  \
  x(TRACE)

#define HTTP_METHOD_ENUM(method) method

/**
 * See the definitions in RFC2616 5.1.1 for the source of this
 * list. Today, proxygen only understands the methods defined in 5.1.1 and
 * is not aware of any extension methods. If you wish to support extension
 * methods, you must handle those separately from this enum.
 */
enum class HTTPMethod {
  HTTP_METHOD_GEN(HTTP_METHOD_ENUM)
};

#undef HTTP_METHOD_ENUM

/**
 * Returns the HTTPMethod that matches the method. Although RFC2616 5.1.1
 * says methods are case-sensitive, we ignore case here since most
 * programmers probably really meant "GET" not "get". If the method is not
 * recognized, the return value will be None
 */
extern boost::optional<HTTPMethod> stringToMethod(folly::StringPiece method);

/**
 * Returns a string representation of the method. If EXTENSION_METHOD is
 * passed, then an empty string is returned
 */
extern const std::string& methodToString(HTTPMethod method);

std::ostream& operator<<(std::ostream& os, HTTPMethod method);

}
