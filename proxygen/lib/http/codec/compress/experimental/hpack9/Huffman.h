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

#include <proxygen/lib/http/codec/compress/Huffman.h>

namespace proxygen { namespace huffman {

const uint32_t kEOSHpack09 = 0x3fffffff;

const HuffTree& huffTree09();

}}
