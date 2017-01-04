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

#include <proxygen/lib/services/AcceptorConfiguration.h>

namespace proxygen {

/**
 * Base class for all the Configuration objects
 */
class ServiceConfiguration {
 public:
  ServiceConfiguration()
  : writeBufferLimit_(4096)
, takeoverEnabled_(false) {}

  virtual ~ServiceConfiguration() {}

  /**
   * Set/get the list of acceptors that will be receiving traffic.
   */
  void setAcceptors(
    const std::list<AcceptorConfiguration> &acceptors) {
    acceptors_.clear();
    acceptors_.insert(acceptors_.begin(), acceptors.begin(), acceptors.end());
  }
  const std::list<AcceptorConfiguration> &getAcceptors() const {
    return acceptors_;
  }

  /**
   * Set/get the amount of data that we're allowed to buffer in-memory before
   * back-pressuring the other end of an HTTP connection.
   */
  void setWriteBufferLimit(uint64_t size) { writeBufferLimit_ = size; }
  uint64_t getWriteBufferLimit() const { return writeBufferLimit_; }

  /**
   * Set/get whether or not we should enable socket takeover
   */
  void setTakeoverEnabled(bool enabled) { takeoverEnabled_ = enabled; }
  bool takeoverEnabled() const { return takeoverEnabled_; }

 private:
  std::list<AcceptorConfiguration> acceptors_;
  uint64_t writeBufferLimit_;
  bool takeoverEnabled_;
};

}
