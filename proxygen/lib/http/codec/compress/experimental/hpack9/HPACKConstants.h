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

#include <proxygen/lib/http/codec/compress/HPACKConstants.h>

namespace proxygen {

namespace HPACK09 {

enum HeaderEncoding : uint8_t {
  LITERAL_INCR_INDEXING = 0x40, // 0100 0000
  TABLE_SIZE_UPDATE = 0x20,// 0010 0000
  LITERAL_NEVER_INDEXING = 0x10,// 0001 0000
  LITERAL_NO_INDEXING = 0x00,   // 0000 0000
  INDEXED = 0x80                // 1000 0000
};

}}
