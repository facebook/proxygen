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

#include <folly/FBString.h>
#include <list>
#include <proxygen/lib/http/codec/compress/HPACKHeaderName.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMHeader.h>
#include <set>
#include <string>
#include <unordered_map>

namespace proxygen {

// QCRAMDeduplication table is used to deduplicate strings in the
// QCRAM header table.  It mints shared_ptr's for the header table,
// and contains an unordered_map of all live strings.  Weak pointers
// are used as values in the map, so whether a string is live
// signalled by whether the weak ptr is not expired.
class QCRAMDeduplicationTable {

  // Returns a pair containing shared_ptr to the one copy of the
  // string, and an size that is 0 if the string was already in the
  // table, or s.size() otherwise..
  std::pair<std::shared_ptr<folly::fbstring>, uint32_t> add(
      const folly::fbstring& s) {
    uint32_t size = 0;
    std::shared_ptr<folly::fbstring> shared;
    std::string key = s.toStdString();
    auto it = dedupTable_.find(key);
    if (it != dedupTable_.end()) {
      auto entry = it->second;
      shared = entry.lock();
    } else {
      shared = std::make_shared<folly::fbstring>(s);
      std::weak_ptr<folly::fbstring> weak = shared;
      dedupTable_.insert({key, weak});
      size = s.size();
    }
    std::pair<std::shared_ptr<folly::fbstring>, uint32_t> result(shared, size);
    return result;
  }

  // Release s, removing from table if it was the last (strong) reference.
  // Returns 0 if an entry remains, otherwize s->size().
  uint32_t remove(std::shared_ptr<folly::fbstring> s) {
    uint32_t size = s->size();
    std::string key = s->toStdString();
    auto it = dedupTable_.find(key);
    CHECK(it != dedupTable_.end());
    auto weak = it->second;
    s.reset();
    if (weak.expired()) {
      dedupTable_.erase(it);
      return size;
    }
    return 0;
  }

 private:
  std::unordered_map<std::string,
                     std::weak_ptr<folly::fbstring>,
                     std::hash<std::string>>
      dedupTable_;
};

class QCRAMNewTableImpl {
 public:
  size_t size() const;
  QCRAMHeader& operator[](size_t i);
  void init(size_t vecSize);
  void resize(size_t size);
  void moveItems(size_t oldTail, size_t oldLength, size_t newLength);
  void add(size_t head,
           const HPACKHeaderName& name,
           const folly::fbstring& value,
           int32_t epoch,
           int32_t packetEpoch);
  bool isVulnerableEpoch(uint32_t i,
                         int32_t commitEpoch,
                         int32_t packetEpoch) const;
  std::vector<QCRAMHeader> vec_;
};

/**
 * Data structure for maintaining indexed headers, based on a fixed-length ring
 * with FIFO semantics. Externally it acts as an array.
 */

class QCRAMHeaderTable {
 public:
  using names_map = std::unordered_map<HPACKHeaderName, std::list<uint32_t>>;

  QCRAMHeaderTable(std::unique_ptr<QCRAMNewTableImpl> table,
                   uint32_t capacityVal)
      : table_(std::move(table)) {
    init(capacityVal);
  }

  ~QCRAMHeaderTable() {
  }
  QCRAMHeaderTable(const QCRAMHeaderTable&) = delete;
  QCRAMHeaderTable& operator=(const QCRAMHeaderTable&) = delete;

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
    // TODO(ckrasic) - might need to increase size of table here.
    readBaseIndex_ = baseIndex;
    writeBaseIndex_ = baseIndex;
  }

  /**
   * Add the header entry at the beginning of the table (index=1)
   *
   * @return true if it was able to add the entry
   */
  bool add(const HPACKHeader& header);
  bool add(const HPACKHeader& header, int32_t epoch, int32_t packetEpoch);

  /**
   * Get the index of the given header, if found.
   *
   * @return 0 in case the header is not found
   */
  uint32_t getIndex(const HPACKHeader& header,
                    bool allowVulnerable,
                    int32_t commitEpoch = -1,
                    int32_t curEpoch = -1) const;

  /**
   * Get the table entry at the given index.
   *
   * @return the header entry
   */
  const QCRAMHeader& getHeader(uint32_t index) const;

  /**
   * Checks if an external index is valid
   */
  bool isValid(uint32_t index) const;

  void addRef(uint32_t i) {
    (*table_)[toInternal(i)].refCount++;
  }

  void subRef(uint32_t i) {
    auto& h = (*table_)[i];
    DCHECK_GT(h.refCount, 0);
    h.refCount--;
  }

  /**
   * @return true if there is at least one header with the given name
   */
  bool hasName(const HPACKHeaderName& headerName);

  bool isVulnerableEpoch(uint32_t index,
                         int32_t commitEpoch,
                         int32_t packetEpoch) const {
    return table_->isVulnerableEpoch(
        toInternal(index), commitEpoch, packetEpoch);
  }

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
  uint32_t nameIndex(const HPACKHeaderName& headerName,
                     bool allowVulnerable,
                     int32_t commitEpoch = -1,
                     int32_t curEpoch = -1) const;

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

  bool operator==(const QCRAMHeaderTable& other) const;

  /**
   * Static versions of the methods that translate indices.
   */
  static uint32_t toExternal(uint32_t head,
                             uint32_t length,
                             uint32_t internalIndex);

  static uint32_t toInternal(uint32_t head,
                             uint32_t length,
                             uint32_t externalIndex);

  /**
   * Translate external index to internal one.
   */
  uint32_t toInternal(uint32_t externalIndex) const;

  uint32_t writeBaseIndex() const {
    return writeBaseIndex_;
  }

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
   * Check that eviction isn't blocked by entries still referenced by
   * un-acknowledged headers.
   */
  bool canEvict(uint32_t needed, uint32_t desiredCapacity);

  /**
   * Move the index to the right.
   */
  uint32_t next(uint32_t i) const;

  /**
   * Move the index to the left.
   */
  uint32_t prev(uint32_t i) const;

  /**
   * Get the index of the tail element of the table.
   */
  uint32_t tail() const;

  /**
   * Translate internal index to external one, including a static version.
   */
  uint32_t toExternal(uint32_t internalIndex) const;

  uint32_t capacity_{0};
  uint32_t bytes_{0}; // size in bytes of the current entries
  std::unique_ptr<QCRAMNewTableImpl> table_;

  uint32_t size_{0}; // how many entries we have in the table
  uint32_t head_{0}; // points to the first element of the ring

  names_map names_;
  int64_t readBaseIndex_{-1};
  int64_t writeBaseIndex_{-1};
};

std::ostream& operator<<(std::ostream& os, const QCRAMHeaderTable& table);

} // namespace proxygen
