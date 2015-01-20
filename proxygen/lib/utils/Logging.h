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

#include <folly/io/IOBuf.h>
#include <ostream>
#include <string>

namespace proxygen {

std::ostream& operator<<(std::ostream& os, const folly::IOBuf* buf);

std::string dumpChain(const folly::IOBuf* buf);

std::string dumpBin(const folly::IOBuf* buf, uint8_t bytes_per_line=8);

void dumpBinToFile(const std::string& filename, const folly::IOBuf* buf);

}
