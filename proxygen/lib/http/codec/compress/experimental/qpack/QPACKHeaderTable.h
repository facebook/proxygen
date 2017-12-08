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
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKHeader.h>
#include <string>
#include <unordered_map>
#include <folly/futures/Future.h>
#include <folly/lang/Bits.h>

namespace proxygen {

/**
 * Data structure for maintaining a QPACK header table, with explicit indexing
 * semantics.
 */

class QPACKHeaderTable {
 public:

  using names_map = std::unordered_map<HPACKHeaderName, std::list<uint32_t>>;

  explicit QPACKHeaderTable(uint32_t capacityVal) {
    init(capacityVal);
  }
  QPACKHeaderTable() {}
  ~QPACKHeaderTable() {}

  QPACKHeaderTable& operator=(const QPACKHeaderTable&) = delete;
  QPACKHeaderTable(const QPACKHeaderTable&) = delete;

  /**
   * Initialize with a given capacity.
   */
  void init(uint32_t capacityVal);

  /**
   * Add the header entry with the given index
   *
   * @return true if it was able to add the entry (eg a subsequent call to
   *              getIndex would return non-zero)
   */
  bool add(const HPACKHeader& header, uint32_t index);

  /**
   * Get the index of the given header, if found.  The 'Ref' version will bump
   * the header's reference count, suitable for encoding headers.
   *
   * @return 0 in case the header is not found
   */
  uint32_t getIndex(const HPACKHeader& header) const;
  uint32_t getIndexRef(const HPACKHeader& header);

  /**
   * Get any index of a header that has the given name. From all the
   * headers with the given name we pick the last one added to the header
   * table, but the way we pick the header can be arbitrary.
   */
  uint32_t nameIndexRef(const HPACKHeaderName& name);

  std::pair<uint32_t, uint32_t> evictNext();

  using DeleteFuture = folly::Future<folly::Unit>;

  /**
   * Remove the index (encoder version).
   *
   * Encoder has a two-step remove, encoderRemove, encoderRemoveAck.  This
   * function will freeze the refcount and prevent the index from being used
   * for future encodings.
   *
   * @return frozen refcount and a future that will be set when the index
   *         removal is ack'd
   */
  std::pair<uint32_t, DeleteFuture> encoderRemove(uint32_t index);
  void encoderRemoveAck(uint32_t index);

  /**
   * Remove the index (decoder version)
   *
   * The index will be deleted as soon as all outstanding references are
   * processed.  Note the caller *should* use onTimeout on the returned
   * DeleteFuture in case some references never arrive.
   */
  DeleteFuture decoderRemove(uint32_t index, uint32_t delRefCount);


  /**
   * DecodeResult hold either a reference to an entry in the table or
   * a table entry itself.  Ownership is passed to the caller when the table
   * would otherwise delete the entry.
   *
   * Fought with boost::variant for a while.
   */
  struct DecodeResult {
    int which;
    const HPACKHeader& ref;
    HPACKHeader value;
    explicit DecodeResult(const HPACKHeader& inRef)
        : which(0),
          ref(inRef) {}
    explicit DecodeResult(HPACKHeader&& inValue)
        : which(1),
          ref(value),
          value(std::move(inValue)) {}
    DecodeResult(DecodeResult&& goner) noexcept
        : which(goner.which),
          ref((which == 0) ? goner.ref : value),
          value(std::move(goner.value)) {
    }
    DecodeResult& operator=(DecodeResult&& goner) noexcept = delete;
  };

  /**
   * Get the table entry at the given index as a future.  The result is either
   * a reference to a header in the table or an rvalue-reference if this is
   * the last use following a delete command.
   *
   * @return the header entry
   */
  using DecodeFuture = folly::Future<DecodeResult>;
  DecodeFuture decodeIndexRef(uint32_t index);

  bool isValid(uint32_t index) const {
    return table_.find(index) != table_.end();
  }

  /**
   * @return true if there is at least one header with the given name
   */
  bool hasName(const HPACKHeaderName& name);

  /**
   * @return the map holding the indexed names
   */
  const names_map& names() const {
    return names_;
  }

  uint32_t nextAvailableIndex() {
    for (size_t i = 0; i < availIndexes_.size(); i++) {
      auto next = folly::findFirstSet(availIndexes_[i]);
      if (next != 0) {
        return i * sizeof(uint64_t) + next;
      }
    }
    return 0;
  }

  /**
   * Table capacity, or maximum number of bytes we can hold.
   */
  uint32_t capacity() const {
    return capacity_;
  }

  /**
   * @return number of valid entries
   */
  uint32_t size() const {
    return table_.size();
  }

  /**
   * @return size in bytes, the sum of the size of all entries
   */
  uint32_t bytes() const {
    return bytes_;
  }

  /**
   * @return number of bytes this encoder is holding that are awaiting
   *         encoderRemoveAck.
   */
  uint32_t pendingDeleteBytes() const {
    return pendingDeleteBytes_;
  }

  bool operator==(const QPACKHeaderTable& other) const;

  void describe(std::ostream& os) const;

 private:
  using Table = std::unordered_map<uint32_t, QPACKHeader>;
  using IteratorPromise = folly::Promise<Table::iterator>;
  using IteratorFuture = folly::Future<Table::iterator>;
  using IteratorPromiseList = std::list<IteratorPromise>;
  using DeletePromise = folly::Promise<folly::Unit>;
  using IndexPromises = std::pair<IteratorPromiseList, DeletePromise>;
  using PromiseMap = std::unordered_map<uint32_t, IndexPromises>;

  IteratorFuture getIterator(uint32_t index);
  void setValueForIndex(uint32_t index, Table::iterator it);
  DeleteFuture getDeleteFuture(uint32_t index);
  void fulfillDeletePromise(uint32_t index);
  IndexPromises& getPromisesForIndex(uint32_t index);

  folly::Optional<HPACKHeader> maybeRemoveIndex(Table::iterator it);
  uint32_t getIndexImpl(const HPACKHeaderName& name,
                        const folly::fbstring& value,
                        bool checkValue, bool takeRef);

  uint32_t capacity_{0};
  uint32_t bytes_{0};     // size in bytes of the current entries
  uint32_t pendingDeleteBytes_{0};  // bytes awaiting delete ack
  Table table_;
  std::array<uint64_t, 8> availIndexes_{
    { ~0lu, ~0lu, ~0lu, ~0lu, ~0lu, ~0lu, ~0lu, ~0lu }};
  names_map names_;
  PromiseMap promises_;
};

std::ostream& operator<<(std::ostream& os, const QPACKHeaderTable& table);

}
