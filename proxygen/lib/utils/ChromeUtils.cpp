/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ChromeUtils.h"

#include <folly/portability/GFlags.h>

#include <string>

namespace proxygen {

int8_t getChromeVersion(folly::StringPiece agent) {
  static const std::string search = "Chrome/";
  auto found = agent.find(search);
  int8_t num = -1;
  VLOG(5) << "The agent is " << agent << " and found is " << found;
  if (found != std::string::npos) {
    auto startNum = found + search.length();
    if (agent.size() > startNum + 3) {
      num = (agent[startNum] - '0') * 10 + (agent[startNum + 1] - '0');
    }
    // Edge claims to be Chrome
    found = agent.find("Edge/");
    if (found != std::string::npos) {
      return -1;
    }
  }
  return num;
}

}
