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
#include <ostream>
#include <string>

namespace proxygen {

class HPACKHeader {
  public:
  static const uint32_t kMinLength = 32;

  HPACKHeader() {}

  HPACKHeader(const std::string& name_,
             const std::string& value_):
    name(name_), value(value_) {}

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

  /**
   * Some header entries don't have a value, see StaticHeaderTable
   */
  bool hasValue() const {
    return !value.empty();
  }

  /**
   * Decide if we will add the current header into the header table
   *
   * This is a way to blacklist some headers that have variable values and are
   * not efficient to index, like a :path with URL params, content-length or
   * headers that contain timestamps: if-modified-since, last-modified.
   * This is not standard for HPACK and it's part of some heuristics used by
   * the encoder to improve the use of the header table.
   *
   * @return true if we should index the header
   */
  bool isIndexable() const;

  std::string name;
  std::string value;
};

std::ostream& operator<<(std::ostream& os, const HPACKHeader& h);

}
