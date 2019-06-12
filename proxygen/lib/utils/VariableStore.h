/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>

#include <folly/Optional.h>
#include <folly/container/F14Map.h>

namespace proxygen {

/**
 * Simple string key-value store.
 */
class VariableStore {
 public:
  folly::Optional<std::string> get(const std::string& id) const {
    auto it = variables_.find(id);
    if (it != variables_.end()) {
      return it->second;
    }

    return folly::none;
  }

  void set(const std::string& id, const std::string& value) {
    variables_[id] = value;
  }

 private:
  folly::F14FastMap<std::string, std::string> variables_;
};

} // namespace proxygen
