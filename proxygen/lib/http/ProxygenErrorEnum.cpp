/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/ProxygenErrorEnum.h>

#define PROXYGEN_ERROR_STR(error) #error

namespace {
  static const char* errorStrings[] = {
    PROXYGEN_ERROR_GEN(PROXYGEN_ERROR_STR)
  };
}

namespace proxygen {

const char* getErrorString(ProxygenError error) {
  if (error < kErrorNone || error >= kErrorMax) {
    return errorStrings[kErrorMax];
  } else {
    return errorStrings[error];
  }
}

const char* getErrorStringByIndex(int i) {
  return errorStrings[i];
}

}
