/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

namespace proxygen { namespace QPACK {

struct Instruction {
  uint8_t instruction;
  uint8_t prefixLength;
};

const Instruction INSERT    { 0x80, 7 };
const Instruction NAME_REF  { 0x00, 7 };
const Instruction DELETE    { 0x00, 6 };
const Instruction DELETE_ACK{ 0x40, 6 };
const Instruction INDEX_REF { 0x80, 6 };
const Instruction LITERAL   { 0x00, 5 };
const Instruction NONE      { 0x00, 8 };

// flag
const uint8_t STATIC_HEADER = 0x40;

}}
