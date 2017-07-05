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

  table_.reserve(getMaxTableLength(capacity_));
  for (uint32_t i = 0; i < getMaxTableLength(capacity_); i++) {
    table_.emplace_back();
  }
  names_.clear();
}

bool HeaderTable::add(const HPACKHeader& header) {

  if (header.bytes() > capacity_) {
    // Per the RFC spec https://tools.ietf.org/html/rfc7541#page-11, we must
    // flush the underlying table if a request is made for a header that is
    // larger than the current table capacity
    reset();
    return false;
  }

  // Make the necessary room in the table if appropriate per RFC spec
  if ((bytes_ + header.bytes()) > capacity_) {
    evict(header.bytes(), capacity_);
  }

  if (size_ > 0) {
    head_ = next(head_);
  }
  table_[head_].name = header.name;
  table_[head_].value = header.value;
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

uint32_t HeaderTable::getMaxTableLength(uint32_t capacityVal) {
  // At a minimum an entry will take 32 bytes
  // No need to add an extra slot; i.e. a capacity of 32 to 63 bytes can hold
  // at most one entry.
  return (capacityVal >> 5);
}

void HeaderTable::removeLast() {
  auto t = tail();
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

void HeaderTable::reset() {
  names_.clear();

  bytes_ = 0;
  size_ = 0;

  // Capacity remains unchanged and for now we leave head_ index the same
}

namespace {
template<class InputIt, class OutputIt>
OutputIt moveItems(InputIt first, InputIt last,
                   OutputIt d_first)
{
  while (first != last) {
    *d_first++ = std::move(*first++);
  }
  return d_first;
}
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
    uint32_t newLength = getMaxTableLength(newCapacity);
    if (newLength > table_.size()) {
      auto oldTail = tail();
      auto oldLength = table_.size();
      table_.resize(newLength);
      if (size_ > 0 && oldTail > head_) {
        // the list wrapped around, need to move oldTail..oldLength to the end
        // of the now-larger table_
        moveItems(table_.begin() + oldTail, table_.begin() + oldLength,
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
  capacity_ = newCapacity;
}

uint32_t HeaderTable::evict(uint32_t needed, uint32_t desiredCapacity) {
  uint32_t previousSize = size_;
  while (size_ > 0 && (bytes_ + needed > desiredCapacity)) {
    removeLast();
  }
  return previousSize - size_;
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
