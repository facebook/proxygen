/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/compress/HPACKHeader.h>

#include <folly/Memory.h>
#include <folly/dynamic.h>
#include <memory>
#include <string>
#include <vector>

namespace proxygen {

class HTTPArchive {
 public:

  std::vector<std::vector<HPACKHeader>> requests;
  std::vector<std::vector<HPACKHeader>> responses;

  static std::unique_ptr<HTTPArchive> fromFile(const std::string& filename);

  // helper function for extracting a list of headers from a json array
  static void extractHeaders(folly::dynamic& obj,
                             std::vector<HPACKHeader>& msg);

  static uint32_t getSize(const std::vector<HPACKHeader>& headers);
};

}
