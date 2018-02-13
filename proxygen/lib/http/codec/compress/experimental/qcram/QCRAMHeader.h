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

#include <proxygen/lib/http/codec/compress/HPACKHeader.h>

#include <folly/Conv.h>
#include <ostream>
#include <string>
#include <vector>

namespace proxygen {

class QCRAMHeader : public HPACKHeader {
 public:
  QCRAMHeader() {}

  QCRAMHeader(folly::StringPiece name_,
              folly::StringPiece value_):
      HPACKHeader(name_, value_) {}

  QCRAMHeader(QCRAMHeader&& goner) noexcept
      : HPACKHeader(std::move(goner)),
        epoch(goner.epoch),
        refCount(goner.refCount),
        dedupName(std::move(goner.dedupName)),
        dedupValue(std::move(goner.dedupValue)) {}

  QCRAMHeader& operator=(QCRAMHeader&& goner) noexcept {
    std::swap(name, goner.name);
    std::swap(value, goner.value);
    epoch = goner.epoch;
    refCount = goner.refCount;
    std::swap(dedupName, goner.dedupName);
    std::swap(dedupValue, goner.dedupValue);
    return *this;
  }

  ~QCRAMHeader() {}

  int32_t epoch{-1};
  uint16_t refCount{0};

  // Since 02, QCRAM supports de-duplication, so entries contain
  // shared pointers to the unique instance of each string.  In the
  // simulation, these are only used for *modelling* the sizes with
  // deduplication.  To really implement it, one would need to change
  // HPACKHeader to use std::shared_ptr for name and value.
  std::shared_ptr<folly::fbstring> dedupName;
  std::shared_ptr<folly::fbstring> dedupValue;

  // The following is for debugging only.
  int32_t packetEpoch{-1};
};

}
