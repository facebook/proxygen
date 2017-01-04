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

#include <string>
#include <folly/Range.h>

namespace proxygen {

// This is a wrapper around openssl base64 encoding.  It is a temporary wrapper
// around openssl, until a more optimized version lands in folly

class Base64 {
  public:
    static std::string urlDecode(const std::string& b64message);
    static std::string urlEncode(folly::ByteRange buffer);
};

}
