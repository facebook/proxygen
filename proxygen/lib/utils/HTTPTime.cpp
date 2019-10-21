/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/utils/HTTPTime.h>

#include <glog/logging.h>

#include <boost/date_time/posix_time/posix_time.hpp>


namespace proxygen {

folly::Optional<int64_t> parseHTTPDateTime(const std::string& s) {
  struct tm tm = {};

  if (s.empty()) {
    return folly::none;
  }

  // Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
  // Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
  // Sun Nov 6 08:49:37 1994        ; ANSI C's asctime() format
  //    Assume GMT as per rfc2616 (see HTTP-date):
  //       - https://www.w3.org/Protocols/rfc2616/rfc2616-sec3.html
  const char* formats[]{"%a, %d %b %Y %H:%M:%S GMT",
                  "%a, %d-%b-%y %H:%M:%S GMT",
                  "%a %b %d %H:%M:%S %Y"};
  std::istringstream input(s);
  input.imbue(std::locale(setlocale(LC_ALL, nullptr)));
  for (const char* f : formats) {
    struct tm tm{};
    if (input >> std::get_time(&tm, f)) {
      return folly::Optional<int64_t>(
#ifdef WIN32
		  _mkgmtime
#else
		  timegm
#endif
		  (&tm));
	}
  }

  LOG(INFO) << "Invalid http time: " << s;
  return folly::none;
}

} // proxygen
