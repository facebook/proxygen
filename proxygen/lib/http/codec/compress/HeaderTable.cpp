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
  // at a minimum an entry will take 32 bytes
  uint32_t length = (capacityVal >> 5) + 1;
  table_.assign(length, HPACKHeader());
  names_.clear();
}

bool HeaderTable::add(const HPACKHeader& header) {
  // handle size overflow
  if (bytes_ + header.bytes() > capacity_) {
    evict(header.bytes());
  }
  // this means the header entry is larger than our table
  if (bytes_ + header.bytes() > capacity_) {
    return false;
  }
  if (size_ > 0) {
    head_ = next(head_);
  }
  table_[head_] = header;
  // index name
  names_[header.name].push_back(head_);
  bytes_ += header.bytes();
  ++size_;
  return true;
}

uint32_t HeaderTable::getIndex(const HPACKHeader& header) const {
  auto it = names_.find(header.name);
  if (it == names_.end()) {
    return 0;
  }
  for (auto i : it->second) {
    if (table_[i].value == header.value) {
      return toExternal(i);
    }
  }
  return 0;
}

bool HeaderTable::hasName(const std::string& name) {
  return names_.find(name) != names_.end();
}

uint32_t HeaderTable::nameIndex(const std::string& name) const {
  auto it = names_.find(name);
  if (it == names_.end()) {
    return 0;
  }
  return toExternal(it->second.back());
}

const HPACKHeader& HeaderTable::operator[](uint32_t i) const {
  CHECK(isValid(i));
  return table_[toInternal(i)];
}

bool HeaderTable::inReferenceSet(uint32_t index) const {
  return refset_.find(toInternal(index)) != refset_.end();
}

bool HeaderTable::isSkippedReference(uint32_t index) const {
  return skippedRefs_.find(toInternal(index)) != skippedRefs_.end();
}

void HeaderTable::clearSkippedReferences() {
  skippedRefs_.clear();
}

void HeaderTable::addSkippedReference(uint32_t index) {
  skippedRefs_.insert(toInternal(index));
}

void HeaderTable::addReference(uint32_t index) {
  refset_.insert(toInternal(index));
}

void HeaderTable::removeReference(uint32_t index) {
  refset_.erase(toInternal(index));
}

void HeaderTable::clearReferenceSet() {
  refset_.clear();
}

list<uint32_t> HeaderTable::referenceSet() const {
  list<uint32_t> external;
  for (auto& i : refset_) {
    external.push_back(toExternal(i));
  }
  // seems like the compiler will avoid the copy here
  return external;
}

void HeaderTable::removeLast() {
  auto t = tail();
  refset_.erase(t);
  skippedRefs_.erase(t);
  // remove the first element from the names index
  auto names_it = names_.find(table_[t].name);
  DCHECK(names_it != names_.end());
  list<uint32_t> &ilist = names_it->second;
  DCHECK(ilist.front() ==t);
  ilist.pop_front();
  // remove the name if there are no indices associated with it
  if (ilist.empty()) {
    names_.erase(names_it);
  }
  bytes_ -= table_[t].bytes();
  --size_;
}

void HeaderTable::setCapacity(uint32_t capacity) {
  // TODO: ddmello - the below is a little dangerous as we update the
  // capacity right away.  Some properties of the class utilize that variable
  // and so might be better to refactor and update capacity at the end of the
  // method (and update other methods)
  auto oldCapacity = capacity_;
  capacity_ = capacity;
  if (capacity_ == oldCapacity) {
    return;
  } else if (capacity_ < oldCapacity) {
    // NOTE: currently no actual resizing is performed...
    evict(0);
  } else {
    // NOTE: due to the above lack of resizing, we must determine whether a
    // resize is actually appropriate (to handle cases where the underlying
    // vector is still >= to the size related to the new capacity requested)
    uint32_t newLength = (capacity_ >> 5) + 1;
    if (newLength > table_.size()) {
      auto oldTail = tail();
      auto oldLength = table_.size();
      table_.resize(newLength);
      if (size_ > 0 && oldTail > head_) {
        // the list wrapped around, need to move oldTail..oldLength to the end
        // of the now-larger table_
        std::copy(table_.begin() + oldTail, table_.begin() + oldLength,
                  table_.begin() + newLength - (oldLength - oldTail));
        // Update the names indecies that pointed to the old range
        for (auto& names_it: names_) {
          for (auto& idx: names_it.second) {
            if (idx >= oldTail) {
              DCHECK_LT(idx + (table_.size() - oldLength), table_.size());
              idx += (table_.size() - oldLength);
            } else {
              // remaining indecies in the list were smaller than oldTail, so
              // should be indexed from 0
              break;
            }
          }
        }
      }
    }
  }
}

uint32_t HeaderTable::evict(uint32_t needed) {
  uint32_t evicted = 0;
  while (size_ > 0 && (bytes_ + needed > capacity_)) {
    removeLast();
    ++evicted;
  }
  return evicted;
}

bool HeaderTable::isValid(uint32_t index) const {
  return 0 < index && index <= size_;
}

uint32_t HeaderTable::next(uint32_t i) const {
  return (i + 1) % table_.size();
}

uint32_t HeaderTable::tail() const {
  return (head_ + table_.size() - size_ + 1) % table_.size();
}

uint32_t HeaderTable::toExternal(uint32_t internalIndex) const {
  return toExternal(head_, table_.size(), internalIndex);
}

uint32_t HeaderTable::toExternal(uint32_t head, uint32_t length,
                                 uint32_t internalIndex) {
  return ((head + length - internalIndex) % length) + 1;
}

uint32_t HeaderTable::toInternal(uint32_t externalIndex) const {
  return toInternal(head_, table_.size(), externalIndex);
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
  list<uint32_t> refset = referenceSet();
  refset.sort();
  list<uint32_t> otherRefset = other.referenceSet();
  otherRefset.sort();
  if (refset != otherRefset) {
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
  os << "reference set: [";
  for (const auto& index : table.referenceSet()) {
    os << index << ", ";
  }
  os << "]" << std::endl;
  os << "total size: " << table.bytes() << std::endl;
  return os;
}

}
