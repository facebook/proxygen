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

#include <proxygen/lib/http/codec/compress/HPACKConstants.h>
#include <proxygen/lib/http/codec/compress/HeaderTable.h>
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKHeaderTable.h>
#include <proxygen/lib/http/codec/compress/StaticHeaderTable.h>

namespace proxygen {

class QPACKContext {
 public:
  explicit QPACKContext(uint32_t tableSize);
  virtual ~QPACKContext() {}

  /**
   * get the index of the given header by looking into both dynamic and static
   * header table
   *
   * @return 0 if cannot be found
   */
  virtual uint32_t getIndex(const HPACKHeader& header);

  /**
   * index of a header entry with the given name from dynamic or static table
   *
   * @return 0 if name not found
   */
  virtual uint32_t nameIndex(const HPACKHeaderName& name);

  /**
   * @return true if the given index points to a static header entry
   */
  virtual bool isStatic(uint32_t index) const;

  /**
   * @return header at the given index by composing dynamic and static tables
   */
  QPACKHeaderTable::DecodeFuture getHeader(uint32_t index);

  const QPACKHeaderTable& getTable() const {
    return table_;
  }

  void seedHeaderTable(std::vector<HPACKHeader>& headers);

  void describe(std::ostream& os) const;

 protected:
  virtual const HeaderTable& getStaticTable() const {
    return StaticHeaderTable::get();
  }

  const HPACKHeader& getStaticHeader(uint32_t index);

  QPACKHeaderTable::DecodeFuture getDynamicHeader(uint32_t index);

  virtual uint32_t globalToDynamicIndex(uint32_t index) const {
    return index - getStaticTable().size();
  }
  virtual uint32_t globalToStaticIndex(uint32_t index) const {
    return index;
  }
  virtual uint32_t dynamicToGlobalIndex(uint32_t index) const {
    return index + getStaticTable().size();
  }
  virtual uint32_t staticToGlobalIndex(uint32_t index) const {
    return index;
  }

  QPACKHeaderTable table_;
};

std::ostream& operator<<(std::ostream& os, const QPACKContext& context);

}
