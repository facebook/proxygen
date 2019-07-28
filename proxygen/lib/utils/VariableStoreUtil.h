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

#include <proxygen/lib/utils/VariableStore.h>

namespace proxygen {

/**
 * Copy a subset of variables from <src> to <dst>.
 */
void copyVars(const std::vector<std::string>& varNames,
              const VariableStore* src,
              VariableStore* dst);

} // namespace proxygen
