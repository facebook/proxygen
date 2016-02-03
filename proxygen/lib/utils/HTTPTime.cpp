/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/utils/HTTPTime.h>

#include <ctime>
#include <iomanip>
#include <sstream>
#include <glog/logging.h>

namespace proxygen {

folly::Optional<int64_t> parseHTTPDateTime(const std::string& s) {
  struct tm tm = {0};

  if (s.empty()) {
    return folly::Optional<int64_t>();
  }

  // Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
  std::istringstream input(s);
  if (input >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S %Z")) {
    return folly::Optional<int64_t>(mktime(&tm));
  }
  input.clear();

  // Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
  input.seekg(0);
  if (input >> std::get_time(&tm, "%a, %d-%b-%y %H:%M:%S %Z")) {
    return folly::Optional<int64_t>(mktime(&tm));
  }
  input.clear();

  // Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format
  input.seekg(0);
  if(input >> std::get_time(&tm, "%a %b %d %H:%M:%S %Y")) {
    return folly::Optional<int64_t>(mktime(&tm));
  }

  LOG(INFO) << "Invalid http time: " << s;
  return folly::Optional<int64_t>();
}

} // proxygen
