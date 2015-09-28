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
#include <proxygen/lib/http/codec/compress/HeaderTable.h>
#include <proxygen/lib/http/codec/compress/StaticHeaderTable.h>

namespace proxygen {

class HPACKContextImpl {
 public:
  static bool isStatic(uint32_t index, size_t staticTableSize) {
    return index <= staticTableSize;
  }

  static uint32_t globalToDynamicIndex(uint32_t index, size_t staticTableSize) {
    return index - staticTableSize;
  }
  static uint32_t globalToStaticIndex(uint32_t index) {
    return index;
  }
  static uint32_t dynamicToGlobalIndex(uint32_t index, size_t staticTableSize) {
    return index + staticTableSize;
  }
  static uint32_t staticToGlobalIndex(uint32_t index) {
    return index;
  }

  static uint32_t getIndex(const HPACKHeader& header,
                           const HeaderTable& staticTable,
                           const HeaderTable& dynamicTable) {
    uint32_t index = staticTable.getIndex(header);
    if (index) {
      return staticToGlobalIndex(index);
    }
    index = dynamicTable.getIndex(header);
    if (index) {
      return dynamicToGlobalIndex(index, staticTable.size());
    }
    return 0;
  }

  static uint32_t nameIndex(const std::string& name,
                            const HeaderTable& staticTable,
                            const HeaderTable& dynamicTable) {
    uint32_t index = staticTable.nameIndex(name);
    if (index) {
      return staticToGlobalIndex(index);
    }
    index = dynamicTable.nameIndex(name);
    if (index) {
      return dynamicToGlobalIndex(index, staticTable.size());
    }
    return 0;
  }
};

}
