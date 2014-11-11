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

#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/HeaderTable.h>
#include <proxygen/lib/http/codec/compress/StaticHeaderTable.h>

namespace proxygen {

class HPACKContext {
 public:
  HPACKContext(HPACK::MessageType msgType,
               uint32_t tableSize);
  virtual ~HPACKContext() {}

  /**
   * get the index of the given header by looking into both dynamic and static
   * header table
   *
   * @return 0 if cannot be found
   */
  uint32_t getIndex(const HPACKHeader& header) const;

  /**
   * index of a header entry with the given name from dynamic or static table
   *
   * @return 0 if name not found
   */
  uint32_t nameIndex(const std::string& name) const;

  /**
   * @return true if the given index points to a static header entry
   */
  bool isStatic(uint32_t index) const;

  /**
   * @return header at the given index by composing dynamic and static tables
   */
  const HPACKHeader& getHeader(uint32_t index);

  const HeaderTable& getTable() const {
    return table_;
  }

 protected:
  virtual const HeaderTable& getStaticTable() const {
    return StaticHeaderTable::get();
  }

  HeaderTable table_;
  HPACK::MessageType msgType_;
};

}
