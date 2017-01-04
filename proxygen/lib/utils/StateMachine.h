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

#include <glog/logging.h>
#include <tuple>

template <typename T>
class StateMachine {
 public:
  typedef typename T::State State;
  typedef typename T::Event Event;

  static State getNewInstance() {
    return T::getInitialState();
  }

  static bool transit(State& state, Event event) {
    bool ok;
    State newState;

    std::tie(newState, ok) = T::find(state, event);
    if (!ok) {
      LOG(ERROR) << "Invalid transition tried: " << state << " " << event;
      return false;
    }
    VLOG(6) << "Transitioning from " << state << " to " << newState;
    state = newState;
    return true;
  }

  static bool canTransit(const State state, Event event) {
    bool ok;

    std::tie(std::ignore, ok) = T::find(state, event);
    return ok;
  }
};
