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

#include <string>
#include <vector>
#include <proxygen/lib/utils/ConsistentHash.h>

namespace proxygen {
/*
 * Weighted Rendezvous Hash is a way to consistently route requests to
 * candidates.
 * Unlike ConsistentHash, Weighted Rendezvous Hash supports the action to
 * reduce the relative weight of a candidate while incurring minimum data
 * movement.
 */
class RendezvousHash : public ConsistentHash {
 public:

  double getMaxErrorRate() const;

  void build(std::vector<std::pair<std::string, uint64_t> >&);

  size_t get(const uint64_t key, const size_t rank = 0) const;

 private:
  uint64_t computeHash(const char* data, size_t len) const;

  uint64_t computeHash(uint64_t i) const;

  std::vector<std::pair<uint64_t, uint64_t>> weights_;
};

} // proxygen
