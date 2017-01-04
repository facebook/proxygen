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

#include <folly/Conv.h>
#include <folly/String.h>
#include <proxygen/lib/utils/Export.h>
#include <string>

namespace proxygen {

// ParseURL can handle non-fully-formed URLs. This class must not persist beyond
// the lifetime of the buffer underlying the input StringPiece

class ParseURL {
 public:
  ParseURL() {}
  explicit ParseURL(folly::StringPiece urlVal) noexcept {
    init(urlVal);
  }

  void init(folly::StringPiece urlVal) {
    CHECK(!initialized_);
    url_ = urlVal;
    parse();
    initialized_ = true;
  }

  folly::StringPiece url() const {
    return url_;
  }

  folly::StringPiece scheme() const {
    return scheme_;
  }

  std::string authority() const {
    return authority_;
  }

  bool hasHost() const {
    return valid() && !host_.empty();
  }

  folly::StringPiece host() const {
    return host_;
  }

  uint16_t port() const {
    return port_;
  }

  std::string hostAndPort() const {
    std::string rc = host_.str();
    if (port_ != 0) {
      folly::toAppend(":", port_, &rc);
    }
    return rc;
  }

  folly::StringPiece path() const {
    return path_;
  }

  folly::StringPiece query() const {
    return query_;
  }

  folly::StringPiece fragment() const {
    return fragment_;
  }

  bool valid() const {
    return valid_;
  }

  folly::StringPiece hostNoBrackets() {
    stripBrackets();
    return hostNoBrackets_;
  }

  bool hostIsIPAddress();

  FB_EXPORT void stripBrackets() noexcept;

 private:
  FB_EXPORT void parse() noexcept;

  void parseNonFully() noexcept;

  bool parseAuthority() noexcept;

  folly::StringPiece url_;
  folly::StringPiece scheme_;
  std::string authority_;
  folly::StringPiece host_;
  folly::StringPiece hostNoBrackets_;
  folly::StringPiece path_;
  folly::StringPiece query_;
  folly::StringPiece fragment_;
  uint16_t port_{0};
  bool valid_{false};
  bool initialized_{false};
};

}
