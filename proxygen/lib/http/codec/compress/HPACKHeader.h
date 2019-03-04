/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Conv.h>
#include <ostream>
#include <string>
#include <proxygen/lib/http/codec/compress/HPACKHeaderName.h>
#include <folly/FBString.h>

namespace proxygen {

class HPACKHeader {
  public:
  static const uint32_t kMinLength = 32;

  HPACKHeader() {}

  HPACKHeader(const HPACKHeaderName& name_,
              const folly::fbstring& value_):
      name(name_), value(value_) {}

  HPACKHeader(folly::StringPiece name_,
              folly::StringPiece value_):
      name(name_), value(value_.data(), value_.size()) {}

  HPACKHeader(HPACKHeader&& goner) noexcept
      : name(std::move(goner.name)),
        value(std::move(goner.value)) {}

  HPACKHeader& operator=(HPACKHeader&& goner) noexcept {
    std::swap(name, goner.name);
    std::swap(value, goner.value);
    return *this;
  }

  ~HPACKHeader() {}

  /**
   * size in bytes of the header entry, as defined in the HPACK spec
   */
  uint32_t bytes() const {
    return folly::to<uint32_t>(kMinLength + name.size() + value.size());
  }

  bool operator==(const HPACKHeader& other) const {
    return name == other.name && value == other.value;
  }
  bool operator<(const HPACKHeader& other) const {
    bool eqname = (name == other.name);
    if (!eqname) {
      return name < other.name;
    }
    return value < other.value;
  }
  bool operator>(const HPACKHeader& other) const {
    bool eqname = (name == other.name);
    if (!eqname) {
      return name > other.name;
    }
    return value > other.value;
  }

  HPACKHeader copy() const {
    return HPACKHeader(name, value);
  }

  /**
   * Some header entries don't have a value, see StaticHeaderTable
   */
  bool hasValue() const {
    return !value.empty();
  }

  HPACKHeaderName name;
  folly::fbstring value;
};

std::ostream& operator<<(std::ostream& os, const HPACKHeader& h);

}
