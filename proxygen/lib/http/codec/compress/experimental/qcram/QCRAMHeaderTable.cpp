/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMContext.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMHeaderTable.h>
#include <folly/experimental/Bits.h>

#include <glog/logging.h>

using std::list;
using std::pair;
using std::string;

namespace proxygen {


size_t QCRAMNewTableImpl::size() const {
  return vec_.size();
}

QCRAMHeader& QCRAMNewTableImpl::operator[] (size_t i) {
  return vec_[i];
}

void QCRAMNewTableImpl::init(size_t vecSize) {
  vec_.reserve(vecSize);
  for (uint32_t i = 0; i < vecSize; i++) {
    vec_.emplace_back();
  }
}

void QCRAMNewTableImpl::resize(size_t size) {
  vec_.resize(size);
}

void QCRAMNewTableImpl::moveItems(size_t oldTail, size_t oldLength, size_t newLength) {
  std::move_backward(vec_.begin() + oldTail, vec_.begin() + oldLength,
                     vec_.begin() + newLength);
}

void QCRAMNewTableImpl::add(size_t head, const HPACKHeaderName& name,
                            const folly::fbstring& value, int32_t epoch, int32_t packetEpoch) {
  VLOG(1) << " add vec[" << head << "] name=" << name << " value=" << value;
  vec_[head].name = name;
  vec_[head].value = value;
  vec_[head].epoch = epoch;
  vec_[head].packetEpoch = packetEpoch;
}

bool QCRAMNewTableImpl::isVulnerableEpoch(uint32_t i,
                                          int32_t commitEpoch,
                  int32_t packetEpoch) const {
  const QCRAMHeader& header = vec_[i];
  int32_t epoch = header.epoch;
  //  return !((epoch <= commitEpoch) || (epoch == packetEpoch));
  return epoch > commitEpoch;
}


void QCRAMHeaderTable::init(uint32_t capacityVal) {
  bytes_ = 0;
  size_ = 0;
  head_ = 0;
  capacity_ = capacityVal;
  uint32_t initLength = getMaxTableLength(capacity_) / 2;
  table_->init(initLength);
  names_.clear();
  VLOG(1) << "table init to capacity " << capacity_;
}

bool QCRAMHeaderTable::add(const HPACKHeader& header) {
  return add(header, -1, -1);
}

bool QCRAMHeaderTable::add(const HPACKHeader& header, int32_t epoch,
                           int32_t packetEpoch) {
  if (header.bytes() > capacity_) {
    // Per the RFC spec https://tools.ietf.org/html/rfc7541#page-11, we must
    // flush the underlying table if a request is made for a header that is
    // larger than the current table capacity
    reset();
    return false;
  }

  // Make the necessary room in the table if appropriate per RFC spec
  if ((bytes_ + header.bytes()) > capacity_) {
    if (!canEvict(header.bytes(), capacity_)) {
      return false;
    }
    evict(header.bytes(), capacity_);
  }

  // TODO(ckrasic) - FIXME: I think this isn't enough for QCRAM, because
  // writeBaseIndex might jump forward due to out of order arrivals.
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
  // TODO(ckrasic) - use dedup table here (or in tableimpl?)
  table_->add(localHead, header.name, header.value, epoch, packetEpoch);
  // index name
  names_[header.name].push_back(localHead);
  bytes_ += header.bytes();
  VLOG(1) << "add header " << header << " at index " << localHead << " bytes is now " << bytes_;
  ++size_;
  return true;
}

uint32_t QCRAMHeaderTable::getIndex(const HPACKHeader& header,
                                    bool allowVulnerable,
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
      uint32_t index = toExternal(i);
      bool vulnerable = isVulnerableEpoch(index, commitEpoch, curEpoch);
      if (vulnerable) {
        if (allowVulnerable) {
          VLOG(2) << "getIndex found match " << header << ":  using vulnerable index " << index;
          return index;
        }
        VLOG(2) << "getIndex found match " << header << ":  can't use vulnerable index " << index;
      } else {
        VLOG(2) << "getIndex found match " << header << ":  using non-vulnerable index " << index;
        return index;
      }
    }
  }
  return encoderHasEntry ? kMaxIndex : 0;
}

bool QCRAMHeaderTable::hasName(const HPACKHeaderName& headerName) {
  return names_.find(headerName) != names_.end();
}

uint32_t QCRAMHeaderTable::nameIndex(const HPACKHeaderName& headerName,
                                     bool allowVulnerable,
                                     int32_t commitEpoch,
                                     int32_t curEpoch) const {
  auto it = names_.find(headerName);
  if (it == names_.end()) {
    return 0;
  }
  for (auto indexIt = it->second.rbegin(); indexIt != it->second.rend();
       ++indexIt) {
    auto i = *indexIt;
    uint32_t index = toExternal(i);
    bool vulnerable = isVulnerableEpoch(index, commitEpoch, curEpoch);
    if (vulnerable) {
      VLOG(2) << "nameIndex found match " << headerName << ":  can't use vulnerable index " << index;
      if (allowVulnerable) {
        return index;
      }
    } else {
      VLOG(2) << "nameIndex found match " << headerName << ":  using non-vulnerable index " << index;
      return index;
    }
  }
  return kMaxIndex;
}

const QCRAMHeader& QCRAMHeaderTable::operator[](uint32_t i) const {
  CHECK(isValid(i));
  VLOG(1) << "[] " << i << " " << toInternal(i);
  return (*table_)[toInternal(i)];
}

uint32_t QCRAMHeaderTable::getMaxTableLength(uint32_t capacityVal) {
  // At a minimum an entry will take 32 bytes
  // No need to add an extra slot; i.e. a capacity of 32 to 63 bytes can hold
  // at most one entry.
  return (capacityVal >> 5);
}

void QCRAMHeaderTable::removeLast() {
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
  VLOG(5) << "Removing local idx=" << t << " name=" << header.name <<
    " value=" << header.value;
  --size_;
}

bool QCRAMHeaderTable::canEvict(uint32_t needed, uint32_t desiredCapacity) {
  uint32_t size = size_;
  uint32_t bytes = bytes_;
  auto t = tail();
  while (size > 0 && (bytes + needed > desiredCapacity)) {
    // remove the first element from the names index
    const auto& header = (*table_)[t];
    if (header.refCount > 0) {
      VLOG(1) << "eviction blocked because entry " << t
              << " has reference count " << header.refCount;
      return false;
    }
    // TODO(ckrasic) - fix this if de-duplication is enabled.
    bytes -= header.bytes();
    --size;
    t = prev(t);
  }
  return (bytes + needed <= desiredCapacity);
}


void QCRAMHeaderTable::reset() {
  names_.clear();

  bytes_ = 0;
  size_ = 0;

  // Capacity remains unchanged and for now we leave head_ index the same
}

void QCRAMHeaderTable::setCapacity(uint32_t newCapacity) {
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

void QCRAMHeaderTable::increaseTableLengthTo(uint32_t newLength) {
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

uint32_t QCRAMHeaderTable::evict(uint32_t needed, uint32_t desiredCapacity) {
  uint32_t previousSize = size_;
  while (size_ > 0 && (bytes_ + needed > desiredCapacity)) {
    removeLast();
  }
  return previousSize - size_;
}

bool QCRAMHeaderTable::isValid(uint32_t index) const {
  return (readBaseIndex_ < 0 && 0 < index && index <= size_) ||
         (readBaseIndex_ >= 0 && 0 < index && index <= table_->size());
}

uint32_t QCRAMHeaderTable::next(uint32_t i) const {
  return (i + 1) % table_->size();
}

uint32_t QCRAMHeaderTable::prev(uint32_t i) const {
  return (i - 1 + table_->size()) % table_->size();
}

uint32_t QCRAMHeaderTable::tail() const {
  // tail is private, and only called in the encoder, where head_ is always
  // valid
  return (head_ + table_->size() - size_ + 1) % table_->size();
}

uint32_t QCRAMHeaderTable::toExternal(uint32_t internalIndex) const {
  return toExternal(readBaseIndex_ >= 0 ? readBaseIndex_ : head_,
                    table_->size(), internalIndex);
}

uint32_t QCRAMHeaderTable::toExternal(uint32_t head, uint32_t length,
                                 uint32_t internalIndex) {
  return ((head + length - internalIndex) % length) + 1;
}

uint32_t QCRAMHeaderTable::toInternal(uint32_t externalIndex) const {
  return toInternal((readBaseIndex_ >= 0) ? readBaseIndex_ : head_,
                    table_->size(), externalIndex);
}

uint32_t QCRAMHeaderTable::toInternal(uint32_t head, uint32_t length,
                                 uint32_t externalIndex) {
  // remove the offset
  --externalIndex;
  return (head + length - externalIndex) % length;
}

bool QCRAMHeaderTable::operator==(const QCRAMHeaderTable& other) const {
  if (size() != other.size()) {
    return false;
  }
  if (bytes() != other.bytes()) {
    return false;
  }
  return true;
}

std::ostream& operator<<(std::ostream& os, const QCRAMHeaderTable& table) {
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
