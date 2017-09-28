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
        epoch(goner.epoch) {}

  QCRAMHeader& operator=(QCRAMHeader&& goner) noexcept {
    std::swap(name, goner.name);
    std::swap(value, goner.value);
    epoch = goner.epoch;
    return *this;
  }

  ~QCRAMHeader() {}

  int32_t epoch{-1};
};

class QCRAMTableImpl : public TableImpl {
 public:
  size_t size() const override { return vec_.size(); }
  HPACKHeader& operator[] (size_t i) override { return vec_[i]; }
  void init(size_t vecSize) override {
    vec_.reserve(vecSize);
    for (uint32_t i = 0; i < vecSize; i++) {
      vec_.emplace_back();
    }
  }
  void resize(size_t size) override { vec_.resize(size); }
  void moveItems(size_t oldTail, size_t oldLength, size_t newLength) override {
    std::move_backward(vec_.begin() + oldTail, vec_.begin() + oldLength,
                       vec_.begin() + newLength);
  }
  void add(size_t head, const HPACKHeaderName& name,
           const folly::fbstring& value, int32_t epoch) override {
    vec_[head].name = name;
    vec_[head].value = value;
    vec_[head].epoch = epoch;
  }

  bool isValidEpoch(uint32_t i, int32_t commitEpoch,
                    int32_t curEpoch) override {
    return (vec_[i].epoch <= commitEpoch || vec_[i].epoch == curEpoch);
  }

  std::vector<QCRAMHeader> vec_;
};

}
