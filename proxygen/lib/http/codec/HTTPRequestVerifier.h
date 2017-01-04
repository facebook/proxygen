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

#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/codec/SPDYUtil.h>
#include <proxygen/lib/http/codec/HTTP2Framer.h>

namespace proxygen {

class HTTPRequestVerifier {
 public:
  explicit HTTPRequestVerifier() {}

  bool setMethod(folly::StringPiece method) {
    if (hasMethod_) {
      error = "Duplicate method";
      return false;
    }
    if (!SPDYUtil::validateMethod(method)) {
      error = "Invalid method";
      return false;
    }
    hasMethod_ = true;
    assert(msg_ != nullptr);
    msg_->setMethod(method);
    return true;
  }

  bool setPath(folly::StringPiece path) {
    if (hasPath_) {
      error = "Duplicate path";
      return false;
    }
    if (!SPDYUtil::validateURL(path)) {
      error = "Invalid url";
      return false;
    }
    hasPath_ = true;
    assert(msg_ != nullptr);
    msg_->setURL(path.str());
    return true;
  }

  bool setScheme(folly::StringPiece scheme) {
    if (hasScheme_) {
      error = "Duplicate scheme";
      return false;
    }
    // This just checks for alpha chars
    if (!SPDYUtil::validateMethod(scheme)) {
      error = "Invalid scheme";
      return false;
    }
    hasScheme_ = true;
    // TODO support non http/https schemes
    if (scheme == http2::kHttps) {
      assert(msg_ != nullptr);
      msg_->setSecure(true);
    }
    return true;
  }

  bool setAuthority(folly::StringPiece authority) {
    if (hasAuthority_) {
      error = "Duplicate authority";
      return false;
    }
    if (!SPDYUtil::validateHeaderValue(authority, SPDYUtil::STRICT)) {
      error = "Invalid authority";
      return false;
    }
    hasAuthority_ = true;
    assert(msg_ != nullptr);
    msg_->getHeaders().add(HTTP_HEADER_HOST, authority.str());
    return true;
  }

  bool validate() {
    if (error.size()) {
      return false;
    }
    if (msg_->getMethod() == HTTPMethod::CONNECT) {
      if (!hasMethod_ || !hasAuthority_ || hasScheme_ || hasPath_) {
        error = folly::to<std::string>("Malformed CONNECT request m/a/s/p=",
                                hasMethod_, hasAuthority_,
                                hasScheme_, hasPath_);
      }
    } else if (!hasMethod_ || !hasScheme_ || !hasPath_) {
      error = folly::to<std::string>("Malformed request m/a/s/p=",
                                hasMethod_, hasAuthority_,
                                hasScheme_, hasPath_);
    }
    return error.empty();
  }

  void setMessage(HTTPMessage* msg) {
    msg_ = msg;
  }

  void setHasMethod(bool hasMethod) {
    hasMethod_ = hasMethod;
  }

  void setHasPath(bool hasPath) {
    hasPath_ = hasPath;
  }

  void setHasScheme(bool hasScheme) {
    hasScheme_ = hasScheme;
  }

  void setHasAuthority(bool hasAuthority) {
    hasAuthority_ = hasAuthority;
  }

  std::string error;

 private:
  HTTPMessage* msg_{nullptr};
  bool hasMethod_{false};
  bool hasPath_{false};
  bool hasScheme_{false};
  bool hasAuthority_{false};
};

} // proxygen
