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

#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/QPACKHeaderTable.h>
#include <proxygen/lib/http/codec/compress/StaticHeaderTable.h>

namespace proxygen {

class QPACKContext {
 public:
  QPACKContext(uint32_t tableSize, bool trackReferences);
  ~QPACKContext() {}

  /**
   * @return header at the given index by composing dynamic and static tables
   */
  const HPACKHeader& getHeader(bool isStatic, uint32_t index, uint32_t base,
                               bool aboveBase);

  const QPACKHeaderTable& getTable() const {
    return table_;
  }

  uint32_t getTableSize() const {
    return table_.capacity();
  }

  uint32_t getBytesStored() const {
    return table_.bytes();
  }

  uint32_t getHeadersStored() const {
    return table_.size();
  }

  void seedHeaderTable(std::vector<HPACKHeader>& headers);

  void describe(std::ostream& os) const;

 protected:
  const StaticHeaderTable& getStaticTable() const {
    return StaticHeaderTable::get();
  }

  QPACKHeaderTable table_;
};

std::ostream& operator<<(std::ostream& os, const QPACKContext& context);

}
