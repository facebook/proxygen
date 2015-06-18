/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>
#include <vector>

namespace proxygen {

class RendezvousHash {
 public:
  explicit RendezvousHash() {}

  void insert(const std::string& key, uint64_t weight);

  std::pair<uint64_t, size_t> get(const uint64_t hash) const;

  std::pair<uint64_t, size_t> get(const char* data, size_t len) const;

  std::pair<uint64_t, size_t> get(const std::string& s) const {
    return get(s.data(), s.length());
  }

  uint64_t computeHash(const char* data, size_t len) const;

  uint64_t computeHash(const std::string& s) const {
    return computeHash(s.data(), s.length());
  }

  uint64_t computeHash(uint64_t i) const;

 private:
  std::vector<std::pair<uint64_t, uint64_t>> weights_;
};

} // proxygen
