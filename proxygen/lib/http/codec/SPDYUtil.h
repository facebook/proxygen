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

#include <assert.h>
#include <cctype>
#include <folly/Range.h>
#include <stdint.h>
#include <string>

namespace proxygen {

class SPDYUtil {
 public:
  // If these are needed elsewhere, we can move them to a more generic
  // namespace/class later
  static const char http_tokens[256];

  static bool validateURL(folly::ByteRange url) {
    for (auto p: url) {
      if (p <= 0x20 || p == 0x7f) {
        // no controls or unescaped spaces
        return false;
      }
    }
    return true;
  }

  static bool validateMethod(folly::ByteRange method) {
    for (auto p: method) {
      if (!isalpha(p)) {
        // methods are all characters
        return false;
      }
    }
    return true;
  }

  static bool validateHeaderName(folly::ByteRange name) {
    if (name.size() == 0) {
      return false;
    }
    for (auto p: name) {
      if (p < 0x80 && http_tokens[(uint8_t)p] != p) {
        return false;
      }
    }
    return true;
  }

  /**
   * RFC2616 allows certain control chars in header values if they are
   * quoted and escaped.
   * When mode is COMPLIANT, then this is allowed.
   * When mode is STRICT, no escaped CTLs are allowed
   */
  enum CtlEscapeMode {
    COMPLIANT,
    STRICT
  };

  static bool validateHeaderValue(folly::ByteRange value,
                                  CtlEscapeMode mode) {
    bool escape = false;
    bool quote = false;
    enum { lws_none,
           lws_expect_nl,
           lws_expect_ws1,
           lws_expect_ws2 } state = lws_none;

    for (auto p = std::begin(value); p != std::end(value); ++p) {
      if (escape) {
        escape = false;
        if (mode == COMPLIANT) {
          // prev char escaped.  Turn off escape and go to next char
          // COMPLIANT mode only
          assert(quote);
          continue;
        }
      }
      switch (state) {
        case lws_none:
          switch (*p) {
            case '\\':
              if (quote) {
                escape = true;
              }
              break;
            case '\"':
              quote = !quote;
              break;
            case '\r':
              state = lws_expect_nl;
              break;
            default:
              if ((*p < 0x20 || *p == 0x7f) && *p != '\t') {
                // unexpected ctl per rfc2616, HT OK
                return false;
              }
              break;
          }
          break;
        case lws_expect_nl:
          if (*p != '\n') {
            // unescaped \r must be LWS
            return false;
          }
          state = lws_expect_ws1;
          break;
        case lws_expect_ws1:
          if (*p != ' ' && *p != '\t') {
            // unescaped \r\n must be LWS
            return false;
          }
          state = lws_expect_ws2;
          break;
        case lws_expect_ws2:
          if (*p != ' ' && *p != '\t') {
            // terminated LWS
            state = lws_none;
            // check this char again
            p--;
          }
          break;
      }
    }
    // Unterminated quotes are OK, since the value can be* TEXT which treats
    // the " like any other char.
    // Unterminated escapes are bad because it will escape the next character
    // when converting to HTTP
    // Unterminated LWS (dangling \r or \r\n) is bad because it could
    // prematurely terminate the headers when converting to HTTP
    return !escape && (state == lws_none || state == lws_expect_ws2);
  }

  static bool hasGzipAndDeflate(const std::string& value, bool& hasGzip,
                                bool& hasDeflate);
};

}
