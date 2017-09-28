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

#include <proxygen/lib/http/codec/compress/HeaderTable.h>
#include <vector>

namespace proxygen {

class HPACKHeaderTableImpl : public TableImpl {
 public:
  size_t size() const override { return vec_.size(); }
  HPACKHeader& operator[] (size_t i) override { return vec_[i]; }
  void init(size_t vecSize) override {
    vec_.reserve(vecSize);
    for (uint32_t i = 0; i < vecSize; i++) {
      vec_.emplace_back();
    }
  }
  void resize(size_t sz) override { vec_.resize(sz); }
  void moveItems(size_t oldTail, size_t oldLength, size_t newLength) override {
    std::move_backward(vec_.begin() + oldTail, vec_.begin() + oldLength,
                       vec_.begin() + newLength);
  }
  void add(size_t head, const HPACKHeaderName& name,
           const folly::fbstring& value) override {
    vec_[head].name = name;
    vec_[head].value = value;
  }

  std::vector<HPACKHeader> vec_;
};

}
