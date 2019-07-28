/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/utils/VariableStoreUtil.h>

namespace proxygen {

void copyVars(const std::vector<std::string>& varNames,
              const VariableStore* src,
              VariableStore* dst) {
  for (const auto& varName : varNames) {
    auto value = src->get(varName);
    if (value.hasValue()) {
      dst->set(varName, *value);
    }
  }
}

} // namespace proxygen
