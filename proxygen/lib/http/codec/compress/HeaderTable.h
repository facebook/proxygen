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

#include <list>
#include <proxygen/lib/http/codec/compress/HPACKHeader.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace proxygen {

class TableImpl {
 public:
  virtual ~TableImpl() {}
  virtual void init(size_t capacity) = 0;
  virtual size_t size() const = 0;
  virtual HPACKHeader& operator[] (size_t i) = 0;
  virtual void resize(size_t size) = 0;
  virtual void moveItems(size_t oldTail, size_t oldLength,
                         size_t newLength) = 0;
  virtual void add(size_t head, const HPACKHeaderName& name,
                   const folly::fbstring& value, int32_t epoch) = 0;
  virtual bool isValidEpoch(uint32_t i, int32_t commitEpoch,
                            int32_t curEpoch) = 0;
};

/**
 * Data structure for maintaining indexed headers, based on a fixed-length ring
 * with FIFO semantics. Externally it acts as an array.
 */

class HeaderTable {
 public:
  typedef std::unordered_map<HPACKHeaderName, std::list<uint32_t>> names_map;

  HeaderTable(std::unique_ptr<TableImpl> table, uint32_t capacityVal)
      : table_(std::move(table)) {
    init(capacityVal);
  }

  ~HeaderTable() {}
  HeaderTable(const HeaderTable&) = delete;
  HeaderTable& operator=(const HeaderTable&) = delete;

  /**
   * Initialize with a given capacity.
   */
  void init(uint32_t capacityVal);

  void setAbsoluteIndexing(bool absoluteIndexing) {
    CHECK_EQ(readBaseIndex_, -1) << "Attempted to change indexing scheme after "
      "encoding has started";
    if (absoluteIndexing) {
      readBaseIndex_ = 0;
      writeBaseIndex_ = 0;
    } else {
      readBaseIndex_ = -1;
      writeBaseIndex_ = -1;
    }
  }

  int64_t markBaseIndex() {
    readBaseIndex_ = writeBaseIndex_;
    return writeBaseIndex_;
  }

  void setBaseIndex(int64_t baseIndex) {
    readBaseIndex_ = baseIndex;
    writeBaseIndex_ = baseIndex;
  }

  /**
   * Add the header entry at the beginning of the table (index=1)
   *
   * @return true if it was able to add the entry
   */
  bool add(const HPACKHeader& header);
  bool add(const HPACKHeader& header, int32_t epoch, bool& eviction);

  /**
   * Get the index of the given header, if found.
   *
   * @return 0 in case the header is not found
   */
  uint32_t getIndex(const HPACKHeader& header, int32_t commitEpoch = -1,
                    int32_t curEpoch = -1) const;

  /**
   * Get the table entry at the given index.
   *
   * @return the header entry
   */
  const HPACKHeader& operator[](uint32_t index) const;

  /**
   * Checks if an external index is valid
   */
  bool isValid(uint32_t index) const;

  /**
   * @return true if there is at least one header with the given name
   */
  bool hasName(const HPACKHeaderName& headerName);

  /**
   * @return the map holding the indexed names
   */
  const names_map& names() const {
    return names_;
  }

  /**
   * Get any index of a header that has the given name. From all the
   * headers with the given name we pick the last one added to the header
   * table, but the way we pick the header can be arbitrary.
   */
  uint32_t nameIndex(const HPACKHeaderName& headerName, int32_t commitEpoch=-1,
                     int32_t curEpoch=-1) const;

  /**
   * Table capacity, or maximum number of bytes we can hold.
   */
  uint32_t capacity() const {
    return capacity_;
  }

  /**
  * Returns the maximum table length required to support HPACK headers given
  * the specified capacity bytes
  */
  uint32_t getMaxTableLength(uint32_t capacityVal);

  /**
   * Sets the current capacity of the header table, and evicts entries
   * if needed.
   */
  void setCapacity(uint32_t capacity);

  /**
   * @return number of valid entries
   */
  uint32_t size() const {
    return size_;
  }

  /**
   * @return size in bytes, the sum of the size of all entries
   */
  uint32_t bytes() const {
    return bytes_;
  }

  /**
   * @return how many slots we have in the table
   */
  size_t length() const {
    return table_->size();
  }

  bool operator==(const HeaderTable& other) const;

  /**
   * Static versions of the methods that translate indices.
   */
  static uint32_t toExternal(uint32_t head, uint32_t length,
                             uint32_t internalIndex);

  static uint32_t toInternal(uint32_t head, uint32_t length,
                             uint32_t externalIndex);

 private:

  /*
   * Increase table length to newLength
   */
  void increaseTableLengthTo(uint32_t newLength);

  /**
   * Removes one header entry from the beginning of the header table.
   */
  void removeLast();

  /**
   * Empties the underlying header table
   */
  void reset();

  /**
   * Evict entries to make space for the needed amount of bytes.
   */
  uint32_t evict(uint32_t needed, uint32_t desiredCapacity);

  /**
   * Move the index to the right.
   */
  uint32_t next(uint32_t i) const;

  /**
   * Get the index of the tail element of the table.
   */
  uint32_t tail() const;

  /**
   * Translate internal index to external one, including a static version.
   */
  uint32_t toExternal(uint32_t internalIndex) const;

  /**
   * Translate external index to internal one.
   */
  uint32_t toInternal(uint32_t externalIndex) const;

  uint32_t capacity_{0};
  uint32_t bytes_{0};     // size in bytes of the current entries
  std::unique_ptr<TableImpl> table_;

  uint32_t size_{0};    // how many entries we have in the table
  uint32_t head_{0};     // points to the first element of the ring

  names_map names_;
  int64_t readBaseIndex_{-1};
  int64_t writeBaseIndex_{-1};
};

std::ostream& operator<<(std::ostream& os, const HeaderTable& table);

}
