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

#include <proxygen/lib/http/codec/compress/HPACKHeader.h>

#include <folly/Conv.h>
#include <ostream>
#include <string>
#include <vector>

namespace proxygen {

class QPACKHeader : public HPACKHeader {
 public:
  QPACKHeader() {}

  QPACKHeader(const std::string& name_,
              const folly::fbstring& value_):
      HPACKHeader(name_, value_) {}

  QPACKHeader(QPACKHeader&& goner) noexcept
      : HPACKHeader(std::move(goner)),
        valid(goner.valid),
        refCount(goner.refCount),
        delRefCount(goner.delRefCount) {}

  QPACKHeader& operator=(QPACKHeader&& goner) noexcept {
    std::swap(name, goner.name);
    std::swap(value, goner.value);
    valid = goner.valid;
    refCount = goner.refCount;
    delRefCount = goner.delRefCount;
    return *this;
  }

  ~QPACKHeader() {}

  bool valid{true};        // set to false when delete requested
  uint16_t refCount{0};
  uint16_t delRefCount{0};
};

}
