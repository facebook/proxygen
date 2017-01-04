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

#include <folly/io/IOBuf.h>
#include <memory>
#include <proxygen/lib/http/codec/compress/HPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/HPACKEncoder.h>
#include <string>

namespace proxygen { namespace hpack {

void dumpToFile(const std::string& filename, const folly::IOBuf* buf);

std::unique_ptr<folly::IOBuf> encodeDecode(
  std::vector<HPACKHeader> headers,
  HPACKEncoder& encoder,
  HPACKDecoder& decoder);

}}
