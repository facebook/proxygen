/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/utils/HTTPTime.h>

#include <glog/logging.h>

#include <folly/portability/Time.h>

namespace proxygen {

folly::Optional<int64_t> parseHTTPDateTime(const std::string& s) {
  struct tm tm = {0};

  if (s.empty()) {
    return folly::Optional<int64_t>();
  }

  // Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
  if (strptime(s.c_str(), "%a, %d %b %Y %H:%M:%S %Z", &tm) != nullptr) {
    return folly::Optional<int64_t>(mktime(&tm));
  }

  // Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
  if (strptime(s.c_str(), "%a, %d-%b-%y %H:%M:%S %Z", &tm) != nullptr) {
    return folly::Optional<int64_t>(mktime(&tm));
  }

  // Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format
  if(strptime(s.c_str(), "%a %b %d %H:%M:%S %Y", &tm) != nullptr) {
    return folly::Optional<int64_t>(mktime(&tm));
  }

  LOG(INFO) << "Invalid http time: " << s;
  return folly::Optional<int64_t>();
}

} // proxygen
