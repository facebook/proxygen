/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/HeaderConstants.h>
#include <proxygen/lib/http/codec/CodecUtil.h>

namespace proxygen {

class HTTPRequestVerifier {
 public:
  explicit HTTPRequestVerifier() {
  }

  void reset(HTTPMessage* msg) {
    msg_ = msg;
    error = "";
    hasMethod_ = false;
    hasPath_ = false;
    hasScheme_ = false;
    hasAuthority_ = false;
    hasUpgradeProtocol_ = false;
  }

  bool setMethod(folly::StringPiece method) {
    if (hasMethod_) {
      error = "Duplicate method";
      return false;
    }
    if (!CodecUtil::validateMethod(method)) {
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
    if (!CodecUtil::validateURL(path)) {
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
    if (!CodecUtil::validateScheme(scheme)) {
      error = "Invalid scheme";
      return false;
    }
    hasScheme_ = true;
    // TODO support non http/https schemes
    if (scheme == headers::kHttps) {
      assert(msg_ != nullptr);
      msg_->setSecure(true);
    } else if (scheme == headers::kMasque) {
      assert(msg_ != nullptr);
      msg_->setMasque();
    }
    return true;
  }

  bool setAuthority(folly::StringPiece authority, bool validate = true) {
    if (hasAuthority_) {
      error = "Duplicate authority";
      return false;
    }
    if (validate &&
        !CodecUtil::validateHeaderValue(authority, CodecUtil::STRICT)) {
      error = "Invalid authority";
      return false;
    }
    hasAuthority_ = true;
    assert(msg_ != nullptr);
    msg_->getHeaders().add(HTTP_HEADER_HOST, authority);
    return true;
  }

  bool setUpgradeProtocol(folly::StringPiece protocol) {
    if (hasUpgradeProtocol_) {
      error = "Duplicate protocol";
      return false;
    }
    setHasUpgradeProtocol(true);
    msg_->setUpgradeProtocol(folly::to<std::string>(protocol));
    return true;
  }

  bool validate() {
    if (error.size()) {
      return false;
    }
    if (msg_->getMethod() == HTTPMethod::CONNECT) {
      if ((!hasUpgradeProtocol_ &&
           (!hasMethod_ || !hasAuthority_ || hasScheme_ || hasPath_)) ||
          (hasUpgradeProtocol_ && (!hasScheme_ || !hasPath_))) {
        error = folly::to<std::string>("Malformed CONNECT request m/a/s/pa/pr=",
                                       hasMethod_,
                                       hasAuthority_,
                                       hasScheme_,
                                       hasPath_,
                                       hasUpgradeProtocol_);
      }
    } else if (hasUpgradeProtocol_ || !hasMethod_ || !hasScheme_ || !hasPath_) {
      error = folly::to<std::string>("Malformed request m/a/s/pa/pr=",
                                     hasMethod_,
                                     hasAuthority_,
                                     hasScheme_,
                                     hasPath_,
                                     hasUpgradeProtocol_);
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

  void setHasUpgradeProtocol(bool val) {
    hasUpgradeProtocol_ = val;
  }

  bool hasUpgradeProtocol() {
    return hasUpgradeProtocol_;
  }

  std::string error;

 private:
  HTTPMessage* msg_{nullptr};
  bool hasMethod_{false};
  bool hasPath_{false};
  bool hasScheme_{false};
  bool hasAuthority_{false};
  bool hasUpgradeProtocol_{false};
};

} // namespace proxygen
