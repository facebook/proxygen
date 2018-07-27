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

#include <unordered_map>
#include <vector>
#include "StructuredHeadersBuffer.h" // @manual=:structured_headers

namespace proxygen {

class StructuredHeadersDecoder {
public:

  explicit StructuredHeadersDecoder(const std::string& s): buf_(s) {}

  StructuredHeaders::DecodeError decodeItem(StructuredHeaderItem& result);

  StructuredHeaders::DecodeError
    decodeList(std::vector<StructuredHeaderItem>& result);

  StructuredHeaders::DecodeError decodeDictionary(Dictionary& result);

  StructuredHeaders::DecodeError
    decodeParameterisedList(ParameterisedList& result);

private:
  enum class MapType {
    DICTIONARY = 0,
    PARAMETERISED_MAP = 1
  };

  StructuredHeaders::DecodeError decodeMap(
    std::unordered_map<std::string, StructuredHeaderItem>& result,
    MapType mapType);

  StructuredHeadersBuffer buf_;
};

}
