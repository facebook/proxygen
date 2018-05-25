/*
 *  Copyright (c) 2015-present, Facebook, Inc.
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
  std::vector<HPACKHeader>& headers,
  HPACKEncoder& encoder,
  HPACKDecoder& decoder);

std::unique_ptr<HPACKDecoder::headers_t> decode(HPACKDecoder& decoder,
                                                const folly::IOBuf* buffer);

std::vector<compress::Header> headersFromArray(
    std::vector<std::vector<std::string>>& a);

std::vector<compress::Header> basicHeaders();
}}
