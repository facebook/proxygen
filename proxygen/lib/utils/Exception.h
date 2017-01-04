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
#include <proxygen/lib/http/ProxygenErrorEnum.h>
#include <string>
#include <utility>

namespace proxygen {

/**
 * Base class for all exceptions.
 */
class Exception : public std::exception {
 public:
  explicit Exception(std::string const& msg);
  Exception(const Exception&);
  Exception(Exception&&) noexcept;

  template <typename... Args>
  explicit Exception(Args&&... args)
      : msg_(folly::to<std::string>(std::forward<Args>(args)...)), code_(0) {}

  ~Exception(void) noexcept override {}

  // std::exception methods
  const char* what(void) const noexcept override;

  // Accessors for code
  void setCode(int code) { code_ = code; }

  int getCode() const { return code_; }

  // Accessors for ProxygenError
  bool hasProxygenError() const { return (proxygenError_ != kErrorNone); }

  void setProxygenError(ProxygenError proxygenError) {
    proxygenError_ = proxygenError;
  }

  ProxygenError getProxygenError() const { return proxygenError_; }

 private:
  const std::string msg_;
  int code_;
  ProxygenError proxygenError_{kErrorNone};
};

}
