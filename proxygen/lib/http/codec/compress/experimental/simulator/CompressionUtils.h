/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/codec/compress/Header.h>

#include <vector>
#include <string>

namespace proxygen {
namespace compress {

// Convert an HTTPMessage into a format that can be passed to a HeaderCodec
std::vector<Header> prepareMessageForCompression(
    const HTTPMessage& msg,
    std::vector<std::string>& cookies);

}

// Compare two HTTPMessage's for equality
bool operator==(const HTTPMessage& msg1, const HTTPMessage& msg2);

}
