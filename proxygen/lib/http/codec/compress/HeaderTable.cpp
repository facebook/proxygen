/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HeaderTable.h>

#include <glog/logging.h>

using std::list;
using std::pair;
using std::string;

namespace proxygen {

void HeaderTable::init(uint32_t capacityVal) {
  bytes_ = 0;
  size_ = 0;
  head_ = 0;
  capacity_ = capacityVal;
  uint32_t initLength = getMaxTableLength(capacity_) / 2;
  table_->init(initLength);
  names_.clear();
}

bool HeaderTable::add(const HPACKHeader& header) {
  bool eviction = false;
  return add(header, -1, eviction);
}

bool HeaderTable::add(const HPACKHeader& header, int32_t epoch,
                      bool& eviction) {
  if (header.bytes() > capacity_) {
    // Per the RFC spec https://tools.ietf.org/html/rfc7541#page-11, we must
    // flush the underlying table if a request is made for a header that is
    // larger than the current table capacity
    eviction = true;
    reset();
    return false;
  }

  // Make the necessary room in the table if appropriate per RFC spec
  eviction = false;
  if ((bytes_ + header.bytes()) > capacity_) {
    evict(header.bytes(), capacity_);
    eviction = true;
  }

  if (size_ == length()) {
    increaseTableLengthTo(std::min( (uint32_t)(size_ * 1.5),
                                    getMaxTableLength(capacity_)));
  }
  uint32_t localHead = head_;
  head_ = next(head_);
  if (writeBaseIndex_ >= 0) {
    localHead = ++writeBaseIndex_ % table_->size();
  } else {
    localHead = head_;
  }
  table_->add(localHead, header.name, header.value, epoch);
  // index name
  names_[header.name].push_back(localHead);
  bytes_ += header.bytes();
  ++size_;
  return true;
}

uint32_t HeaderTable::getIndex(const HPACKHeader& header,
                               int32_t commitEpoch,
                               int32_t curEpoch) const {
  auto it = names_.find(header.name);
  if (it == names_.end()) {
    return 0;
  }
  bool encoderHasEntry = false;
  for (auto i : it->second) {
    if ((*table_)[i].value == header.value) {
      encoderHasEntry = true;
      if (table_->isValidEpoch(i, commitEpoch, curEpoch)) {
        return toExternal(i);
      }
    }
  }
  return encoderHasEntry ? std::numeric_limits<uint32_t>::max() : 0;
}

bool HeaderTable::hasName(const HPACKHeaderName& headerName) {
  return names_.find(headerName) != names_.end();
}

uint32_t HeaderTable::nameIndex(const HPACKHeaderName& headerName,
                                int32_t commitEpoch,
                                int32_t curEpoch) const {
  auto it = names_.find(headerName);
  if (it == names_.end()) {
    return 0;
  }
  for (auto indexIt = it->second.rbegin(); indexIt != it->second.rend();
       ++indexIt) {
    auto i = *indexIt;
    if (table_->isValidEpoch(i, commitEpoch, curEpoch)) {
      return toExternal(i);
    }
  }
  return 0;
}

const HPACKHeader& HeaderTable::operator[](uint32_t i) const {
  CHECK(isValid(i));
  return (*table_)[toInternal(i)];
}

uint32_t HeaderTable::getMaxTableLength(uint32_t capacityVal) {
  // At a minimum an entry will take 32 bytes
  // No need to add an extra slot; i.e. a capacity of 32 to 63 bytes can hold
  // at most one entry.
  return (capacityVal >> 5);
}

void HeaderTable::removeLast() {
  auto t = tail();
  // remove the first element from the names index
  auto names_it = names_.find((*table_)[t].name);
  DCHECK(names_it != names_.end());
  auto &ilist = names_it->second;
  if (writeBaseIndex_ < 0) {
    DCHECK_EQ(ilist.front(), t);
  } // otherwise, we may have written out of order.

  for (auto indexIt = ilist.begin(); indexIt != ilist.end(); ++indexIt) {
    if (*indexIt == t) {
      ilist.erase(indexIt);
      break;
    }
  }
  // remove the name if there are no indices associated with it
  if (ilist.empty()) {
    names_.erase(names_it);
  }
  const auto& header = (*table_)[t];
  bytes_ -= header.bytes();
  VLOG(10) << "Removing local idx=" << t << " name=" << header.name <<
    " value=" << header.value;
  --size_;
}

void HeaderTable::reset() {
  names_.clear();

  bytes_ = 0;
  size_ = 0;

  // Capacity remains unchanged and for now we leave head_ index the same
}

void HeaderTable::setCapacity(uint32_t newCapacity) {
  if (newCapacity == capacity_) {
    return;
  } else if (newCapacity < capacity_) {
    // NOTE: currently no actual resizing is performed...
    evict(0, newCapacity);
  } else {
    // NOTE: due to the above lack of resizing, we must determine whether a
    // resize is actually appropriate (to handle cases where the underlying
    // vector is still >= to the size related to the new capacity requested)
    uint32_t newLength = getMaxTableLength(newCapacity) / 2;
    if (newLength > table_->size()) {
      increaseTableLengthTo(newLength);
    }
  }
  capacity_ = newCapacity;
}

void HeaderTable::increaseTableLengthTo(uint32_t newLength) {
  DCHECK_GE(newLength, length());
  auto oldTail = tail();
  auto oldLength = table_->size();
  table_->resize(newLength);
  // TODO: referenence to head here is incompatible with baseIndex
  if (size_ > 0 && oldTail > head_) {
    // the list wrapped around, need to move oldTail..oldLength to the end
    // of the now-larger table_
    table_->moveItems(oldTail, oldLength, newLength);
    // Update the names indecies that pointed to the old range
    for (auto& names_it: names_) {
      for (auto& idx: names_it.second) {
        if (idx >= oldTail) {
          DCHECK_LT(idx + (table_->size() - oldLength), table_->size());
          idx += (table_->size() - oldLength);
        } else {
          // remaining indecies in the list were smaller than oldTail, so
          // should be indexed from 0
          break;
        }
      }
    }
  }
}

uint32_t HeaderTable::evict(uint32_t needed, uint32_t desiredCapacity) {
  uint32_t previousSize = size_;
  while (size_ > 0 && (bytes_ + needed > desiredCapacity)) {
    removeLast();
  }
  return previousSize - size_;
}

bool HeaderTable::isValid(uint32_t index) const {
  return (readBaseIndex_ < 0 && 0 < index && index <= size_) ||
         (readBaseIndex_ >= 0 && 0 < index && index <= table_->size());
}

uint32_t HeaderTable::next(uint32_t i) const {
  return (i + 1) % table_->size();
}

uint32_t HeaderTable::tail() const {
  // tail is private, and only called in the encoder, where head_ is always
  // valid
  return (head_ + table_->size() - size_ + 1) % table_->size();
}

uint32_t HeaderTable::toExternal(uint32_t internalIndex) const {
  return toExternal(readBaseIndex_ >= 0 ? readBaseIndex_ : head_,
                    table_->size(), internalIndex);
}

uint32_t HeaderTable::toExternal(uint32_t head, uint32_t length,
                                 uint32_t internalIndex) {
  return ((head + length - internalIndex) % length) + 1;
}

uint32_t HeaderTable::toInternal(uint32_t externalIndex) const {
  return toInternal((readBaseIndex_ >= 0) ? readBaseIndex_ : head_,
                    table_->size(), externalIndex);
}

uint32_t HeaderTable::toInternal(uint32_t head, uint32_t length,
                                 uint32_t externalIndex) {
  // remove the offset
  --externalIndex;
  return (head + length - externalIndex) % length;
}

bool HeaderTable::operator==(const HeaderTable& other) const {
  if (size() != other.size()) {
    return false;
  }
  if (bytes() != other.bytes()) {
    return false;
  }
  return true;
}

std::ostream& operator<<(std::ostream& os, const HeaderTable& table) {
  os << std::endl;
  for (size_t i = 1; i <= table.size(); i++) {
    const HPACKHeader& h = table[i];
    os << '[' << i << "] (s=" << h.bytes() << ") "
       << h.name << ": " << h.value << std::endl;
  }
  os << "total size: " << table.bytes() << std::endl;
  return os;
}

}
