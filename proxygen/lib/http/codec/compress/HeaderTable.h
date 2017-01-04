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

/**
 * Data structure for maintaining indexed headers, based on a fixed-length ring
 * with FIFO semantics. Externally it acts as an array.
 */

class HeaderTable {
 public:

  typedef std::unordered_map<std::string, std::list<uint32_t>> names_map;

  explicit HeaderTable(uint32_t capacityVal) {
    init(capacityVal);
  }
  HeaderTable() {}

  ~HeaderTable() {}

  /**
   * Initialize with a given capacity.
   */
  void init(uint32_t capacityVal);

  /**
   * Add the header entry at the beginning of the table (index=1) and add the
   * index to the reference set.
   *
   * @return true if it was able to add the entry
   */
  bool add(const HPACKHeader& header);

  /**
   * Get the index of the given header, if found.
   *
   * @return 0 in case the header is not found
   */
  uint32_t getIndex(const HPACKHeader& header) const;

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
  bool hasName(const std::string& name);

  /**
   * @return the map holding the indexed names
   *
   * Note: this contains references to internal indices, so it's useful only
   * for testing or instrumentation.
   */
  const names_map& names() const {
    return names_;
  }

  /**
   * Get any index of a header that has the given name. From all the
   * headers with the given name we pick the last one added to the header
   * table, but the way we pick the header can be arbitrary.
   */
  uint32_t nameIndex(const std::string& name) const;

  /**
   * Clear new references set
   */
  void clearSkippedReferences();

  /**
   * Tests whether the given index is a new reference.
   */
  bool isSkippedReference(uint32_t index) const;

  /**
   * Keep record of the given entry as a skipped reference.
   */
  void addSkippedReference(uint32_t index);

  /**
   * Check if a given index is part of the reference set.
   */
  bool inReferenceSet(uint32_t index) const;

  /**
   * Add index to the reference set.
   */
  void addReference(uint32_t index);

  /**
   * Remove index from the reference set.
   */
  void removeReference(uint32_t index);

  /**
   * Create a list with all the indices that are in the reference set. The
   * caller will have ownership on the returned list.
   */
  std::list<uint32_t> referenceSet() const;

  /**
   * Remove all indices from the reference set.
   */
  void clearReferenceSet();

  /**
   * Table capacity, or maximum number of bytes we can hold.
   */
  uint32_t capacity() const {
    return capacity_;
  }

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
    return table_.size();
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
  HeaderTable& operator=(const HeaderTable&); // non-copyable

  /**
   * Removes one header entry from the beginning of the header table.
   */
  void removeLast();

  /**
   * Evict entries to make space for the needed amount of bytes.
   */
  uint32_t evict(uint32_t needed);

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
  std::vector<HPACKHeader> table_;

  uint32_t size_{0};    // how many entries we have in the table
  uint32_t head_{0};     // points to the first element of the ring

  names_map names_;
  std::unordered_set<uint32_t> refset_;
  std::unordered_set<uint32_t> skippedRefs_;
};

std::ostream& operator<<(std::ostream& os, const HeaderTable& table);

}
