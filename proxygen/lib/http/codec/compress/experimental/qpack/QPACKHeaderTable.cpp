/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKHeaderTable.h>
#include <folly/experimental/Bits.h>

#include <glog/logging.h>

using std::list;
using std::pair;
using std::string;

namespace proxygen {

void QPACKHeaderTable::init(uint32_t capacityVal) {
  bytes_ = 0;
  capacity_ = capacityVal;
  DCHECK_LE((capacityVal >> 5) / 64  + 1, availIndexes_.size());

  names_.clear();
  table_.clear();
}

bool QPACKHeaderTable::add(const HPACKHeader& header, uint32_t index) {
  // Make the necessary room in the table if appropriate per RFC spec
  if (index == 0) {
    return false;
  }
  if ((bytes_ + header.bytes()) > capacity_) {
    // error, exceeded max size
    return false;
  }

  auto it = table_.find(index);
  if (it != table_.end()) {
    if (!(header == it->second)) {
      // fatal error, multiple insert at occupied index
      // bool is insufficient to demarcate fatal and non-fatal
      return false;
    }
    it->second.refCount++;
    return !maybeRemoveIndex(it).hasValue();
  }
  bool inserted;
  std::tie(it, inserted) = table_.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(index),
    std::forward_as_tuple(header.name.get(), header.value));
  folly::Bits<uint64_t>::clear(availIndexes_.begin(), index - 1);
  // index name
  it->second.refCount = 1;
  names_[header.name].push_back(index);
  bytes_ += header.bytes();
  setValueForIndex(index, it);
  // Don't call maybeRemoveIndex here, setValueForIndex will handle in callbacks
  // just return true if it's still in the table.
  return table_.find(index) != table_.end();
}

uint32_t QPACKHeaderTable::getIndex(const HPACKHeader& header) const {
  // cast away const-ness
  return ((QPACKHeaderTable*)this)->getIndexImpl(
    header.name, header.value, true, false);
}

uint32_t QPACKHeaderTable::getIndexRef(const HPACKHeader& header) {
  return getIndexImpl(header.name, header.value, true, true);
}

uint32_t QPACKHeaderTable::nameIndexRef(const HPACKHeaderName& name) {
  std::string empty_string;
  return getIndexImpl(name, empty_string, false, true);
}

uint32_t QPACKHeaderTable::getIndexImpl(const HPACKHeaderName& name,
                                        const folly::fbstring& value,
                                        bool checkValue, bool takeRef) {
  auto nameIt = names_.find(name);
  if (nameIt == names_.end()) {
    return 0;
  }
  for (auto idx: nameIt->second) {
    auto it = table_.find(idx);
    CHECK(it != table_.end());
    if (it->second.valid && (!checkValue || it->second.value == value)) {
      if (takeRef) {
        it->second.refCount++;
        // encoder side operation, getIndex should never delete
        CHECK(!maybeRemoveIndex(it).hasValue());
      }
      return idx;
    }
  }
  return 0;
}

std::pair<uint32_t, uint32_t> QPACKHeaderTable::evictNext() {
  // this is lame for perf
  // evict lowest refcount
  auto minIt = table_.end();
  for (auto it = table_.begin(); it != table_.end(); ++it) {
    if (it->second.valid &&
        (minIt == table_.end() ||
         it->second.refCount < minIt->second.refCount)) {
      minIt = it;
    }
  }
  if (minIt == table_.end()) {
    VLOG(4) << "Could not find entry to evict";
    return {0, 0};
  }
  return {minIt->first, minIt->second.bytes()};
}

std::pair<uint32_t, QPACKHeaderTable::DeleteFuture>
QPACKHeaderTable::encoderRemove(uint32_t index) {
  auto it = table_.find(index);
  CHECK(it != table_.end());
  it->second.valid = false;
  it->second.delRefCount = it->second.refCount;
  pendingDeleteBytes_ += it->second.bytes();
  VLOG(4) << "Evicting h=" << it->second << " refcount=" << it->second.refCount;
  return {it->second.refCount, getDeleteFuture(index)};
}

void
QPACKHeaderTable::encoderRemoveAck(uint32_t index) {
  auto it = table_.find(index);
  CHECK(it != table_.end()); // or illegal QPACK-ACK
  CHECK_GE(pendingDeleteBytes_, it->second.bytes());
  pendingDeleteBytes_ -= it->second.bytes();
  CHECK(maybeRemoveIndex(it).hasValue());
}

QPACKHeaderTable::DeleteFuture QPACKHeaderTable::decoderRemove(
  uint32_t index, uint32_t delRefCount) {
  return getIterator(index)
    .then([this, index, delRefCount] (Table::iterator it) {
        if (delRefCount < it->second.refCount) {
          getPromisesForIndex(index).second.setException(
            std::runtime_error(
              folly::to<string>("invalid refcount table refcount=",
                                it->second.refCount, " delRefCount=",
                                delRefCount)));
        }
        it->second.valid = false;
        it->second.delRefCount = delRefCount;
        if (maybeRemoveIndex(it).hasValue()) {
          // discards returned header if deletion is done
          return folly::makeFuture();
        } else {
          VLOG(5) << "index=" << index << " refcount=" << it->second.refCount
                  << " delRefCount=" << delRefCount;
          return getDeleteFuture(index);
        }
      });
}

folly::Optional<HPACKHeader> QPACKHeaderTable::maybeRemoveIndex(
  Table::iterator it) {
  CHECK(it != table_.end());
  if (it->second.valid || it->second.delRefCount == 0 ||
      it->second.refCount < it->second.delRefCount) {
    // This index is not ready for removal
    return folly::none;
  }
  // remove the first element from the names index
  auto names_it = names_.find(it->second.name);
  DCHECK(names_it != names_.end());
  list<uint32_t> &ilist = names_it->second;

  for (auto indexIt = ilist.begin(); indexIt != ilist.end(); ++indexIt) {
    if (*indexIt == it->first) {
      ilist.erase(indexIt);
      break;
    }
  }

  // remove the name if there are no indices associated with it
  if (ilist.empty()) {
    names_.erase(names_it);
  }
  bytes_ -= it->second.bytes();
  HPACKHeader header(std::move(it->second));
  auto index = it->first;
  folly::Bits<uint64_t>::set(availIndexes_.begin(), index - 1);
  VLOG(5) << "Dropping h=" << header;
  table_.erase(it);
  fulfillDeletePromise(index);
  return folly::Optional<HPACKHeader>(std::move(header));
}

bool QPACKHeaderTable::hasName(const HPACKHeaderName& name) {
  return names_.find(name) != names_.end();
}

QPACKHeaderTable::DecodeFuture
QPACKHeaderTable::decodeIndexRef(uint32_t index) {
  return getIterator(index)
    .then([this] (Table::iterator it) {
        it->second.refCount++;
        auto res = maybeRemoveIndex(it);
        if (res.hasValue()) {
          // Deleted from table, 'it' is invalid, return the value
          return folly::makeFuture<DecodeResult>(
            DecodeResult(std::move(res.value())));
        }
        // still good
        return folly::makeFuture<DecodeResult>(DecodeResult(it->second));
      });
}

bool QPACKHeaderTable::operator==(const QPACKHeaderTable& other) const {
  if (size() != other.size()) {
    return false;
  }
  if (bytes() != other.bytes()) {
    return false;
  }
  return true;
}

QPACKHeaderTable::IteratorFuture
QPACKHeaderTable::getIterator(uint32_t index) {
  auto it = table_.find(index);
  if (it != table_.end()) {
    return folly::makeFuture<Table::iterator>(std::move(it));
  }
  IteratorPromiseList& valuePromises = getPromisesForIndex(index).first;
  valuePromises.emplace_back();
  return valuePromises.back().getFuture();
}

void
QPACKHeaderTable::setValueForIndex(uint32_t index, Table::iterator it) {
  auto pIt = promises_.find(index);
  if (pIt == promises_.end()) {
    return;
  }
  IteratorPromiseList valuePromises = std::move(pIt->second.first);
  for (bool empty = valuePromises.empty(); !empty;) {
    IteratorPromise promise = std::move(valuePromises.front());
    valuePromises.pop_front();
    empty = valuePromises.empty();
    if (it == table_.end()) {
      promise.setException(std::runtime_error("decode error"));
    } else {
      promise.setValue(it);
    }
    // it might be invalid if there was a delete with the wrong refCount
    // eg: delete 1, decode, add; or delete 1, delete
    if (!empty) {
      it = table_.find(index);
    }
  }
}

QPACKHeaderTable::DeleteFuture
QPACKHeaderTable::getDeleteFuture(uint32_t index) {
  return getPromisesForIndex(index).second.getFuture();
}

void
QPACKHeaderTable::fulfillDeletePromise(uint32_t index) {
  auto it = promises_.find(index);
  if (it == promises_.end()) {
    return;
  }
  it->second.second.setValue();
  CHECK(it->second.first.empty());
  promises_.erase(it);
}

QPACKHeaderTable::IndexPromises&
QPACKHeaderTable::getPromisesForIndex(uint32_t index) {
  auto it = promises_.find(index);
  if (it == promises_.end()) {
    it = promises_.emplace(std::piecewise_construct,
                           std::forward_as_tuple(index),
                           std::forward_as_tuple()).first;
  }
  return it->second;
}

void
QPACKHeaderTable::describe(std::ostream& os) const {
  os << std::endl;
  for (auto &h: table_) {
    os << '[' << h.first << "] (s=" << h.second.bytes() << ") "
       << h.second.name << ": " << h.second.value << std::endl;
  }
  os << "total size: " << bytes() << std::endl;
}

std::ostream& operator<<(std::ostream& os, const QPACKHeaderTable& table) {
  table.describe(os);
  return os;
}

}
