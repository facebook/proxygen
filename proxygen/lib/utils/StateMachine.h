/*
 *  Copyright (c) 2015-present, Facebook, Inc.
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

namespace proxygen {

template <typename T>
class StateMachine {
 public:
  using State = typename T::State;
  using Event = typename T::Event;

  static State getNewInstance() {
    return T::getInitialState();
  }

  static bool transit(State& state, Event event) {
    bool ok;
    State newState;

    std::tie(newState, ok) = T::find(state, event);
    if (!ok) {
      LOG(ERROR) << T::getName() << ": invalid transition tried: " << state
                 << " " << event;
      return false;
    } else {
      VLOG(6) << T::getName() << ": transitioning from " << state << " to "
              << newState;
      state = newState;
      return true;
    }
  }

  static bool canTransit(const State state, Event event) {
    bool ok;

    std::tie(std::ignore, ok) = T::find(state, event);
    return ok;
  }
};

}
