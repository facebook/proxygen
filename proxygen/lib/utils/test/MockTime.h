/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <glog/logging.h>
#include <proxygen/lib/utils/Time.h>

namespace proxygen {

class MockTimeUtil : public TimeUtil {
 public:

  void advance(std::chrono::milliseconds ms) {
    t_ += ms;
  }

  void setCurrentTime(TimePoint t) {
    CHECK(t.time_since_epoch() > t_.time_since_epoch())
      << "Time can not move backwards";
    t_ = t;
  }

  void verifyAndClear() {
  }

  TimePoint now() const override {
    return t_;
  }

 private:
  TimePoint t_;
};

}
