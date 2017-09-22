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

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <iostream>
#include <boost/variant.hpp>
#include <proxygen/lib/http/HTTPCommonHeaders.h>

namespace proxygen {

/*
 * HPACKHeaderName stores the header name of HPACKHeaders. If
 * the header name is in HTTPCommonHeader, it will store a
 * pointer to the common header, otherwise, it will store a
 * pointer to a the dynamically allocated std::string
 */
class HPACKHeaderName {
 public:
  HPACKHeaderName() {}

  explicit HPACKHeaderName(folly::StringPiece name) {
    storeAddress(name);
  }
  HPACKHeaderName(const HPACKHeaderName& headerName) {
    copyAddress(headerName);
  }
  HPACKHeaderName(HPACKHeaderName&& goner) noexcept {
    moveAddress(goner);
  }
  void operator=(folly::StringPiece name) {
    resetAddress();
    storeAddress(name);
  }
  void operator=(const HPACKHeaderName& headerName) {
    resetAddress();
    copyAddress(headerName);
  }
  void operator=(HPACKHeaderName&& goner) noexcept {
    resetAddress();
    moveAddress(goner);
  }

  ~HPACKHeaderName() {
    resetAddress();
  }

  /*
   * Compare the strings stored in HPACKHeaderName
   */
  bool operator==(const HPACKHeaderName& headerName) const {
    return *getAddress() == *headerName.getAddress();
  }
  bool operator!=(const HPACKHeaderName& headerName) const {
    return *getAddress() != *headerName.getAddress();
  }
  bool operator>(const HPACKHeaderName& headerName) const {
    return *getAddress() > *headerName.getAddress();
  }
  bool operator<(const HPACKHeaderName& headerName) const {
    return *getAddress() < *headerName.getAddress();
  }
  bool operator>=(const HPACKHeaderName& headerName) const {
    return *getAddress() >= *headerName.getAddress();
  }
  bool operator<=(const HPACKHeaderName& headerName) const {
    return *getAddress() <= *headerName.getAddress();
  }

  /*
   * Return std::string stored in HPACKHeaderName
   */
  const std::string& get() const {
    return *getAddress();
  }

  /*
   * Return whether or not address points to header
   * name in HTTPCommonHeaders
   */
  bool isCommonName() {
    return !isAllocated();
  }

  /*
   * Directly call std::string member functions
   */
  uint32_t size() const {
    return (uint32_t)(getAddress()->size());
  }
  const char* data() {
    return getAddress()->data();
  }
  const char* c_str() const {
    return getAddress()->c_str();
  }

 private:
  /*
   * Store the address to either common header or newly allocated string
   */
  void storeAddress(folly::StringPiece name) {
    HTTPHeaderCode headerCode = HTTPCommonHeaders::hash(
      name.data(), name.size());
    if (headerCode == HTTPHeaderCode::HTTP_HEADER_NONE ||
        headerCode == HTTPHeaderCode::HTTP_HEADER_OTHER) {
      std::string* newAddress = new std::string(name.size(), 0);
      std::transform(name.begin(), name.end(), newAddress->begin(), ::tolower);
      address = newAddress;
      setAllocationFlag();
    } else {
      address = HTTPCommonHeaders::getPointerToHeaderName(headerCode, true);
    }
  }

  /*
   * Copy the address from another HPACKHeaderName
   */
  void copyAddress(const HPACKHeaderName& headerName) {
    if (headerName.isAllocated()) {
      address = new std::string(headerName.get());
      setAllocationFlag();
    } else {
      address = headerName.getAddress();
    }
  }

  /*
   * Move the address from another HPACKHeaderName
   */
  void moveAddress(HPACKHeaderName& goner) {
    address = goner.address;
#if !FOLLY_X64
    allocated_ = goner.allocated_;
#endif
    goner.removeAllocationFlag();
  }

  /*
   * Delete the address and any allocated memory
   */
  void resetAddress() {
    if (isAllocated()) {
      delete getAddress();
    }
    removeAllocationFlag();
  }

  /*
   * Mask the least significant byte to get actual address, or if not
   * FOLLY_X64 architecture, return address
   */
  const std::string* getAddress() const {
#if FOLLY_X64
    return reinterpret_cast<const std::string*>(
               reinterpret_cast<uintptr_t>(address) & ~0x1);
#else
    return address;
#endif
  }

  /*
   * Return the least significant bit to see if an std::string was allocated,
   * or if not FOLLY_X64 architecture, return allocated_
   */
  bool isAllocated() const {
#if FOLLY_X64
    return reinterpret_cast<uintptr_t>(address) & 0x1;
#else
    return allocated_;
#endif
  }

  /*
   * Flip the least significant bit to 1 to keep track of allocation,
   * or if not FOLLY_X64 architecture, set allocated_ to true
   */
  void setAllocationFlag() {
#if FOLLY_X64
    uintptr_t modifiableAddress =  reinterpret_cast<uintptr_t>(address);
    address = reinterpret_cast<std::string*>(modifiableAddress |= 0x1);
#else
    allocated_ = true;
#endif
  }

  void removeAllocationFlag() {
#if !FOLLY_X64
    allocated_ = false;
#endif
    address = nullptr;
  }

#if !FOLLY_X64
    /*
     * If system is not byte aligned, use boolean to track allocation
     */
    bool allocated_ = false;
#endif

  /*
   * Address either stores a pointer to a header name in HTTPCommonHeaders,
   * or stores a pointer to a dynamically allocated std::string
   */
  const std::string* address = nullptr;
};

inline std::ostream& operator<<(std::ostream& os, const HPACKHeaderName& name) {
  os << name.get();
  return os;
}

} // proxygen

namespace std {

template<>
struct hash<proxygen::HPACKHeaderName> {
  size_t operator()(const proxygen::HPACKHeaderName& headerName) const {
    return std::hash<std::string>()(headerName.get());
  }
};

} // std
