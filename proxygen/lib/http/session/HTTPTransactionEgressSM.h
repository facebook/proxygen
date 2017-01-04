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

#include <map>
#include <iosfwd>
#include <proxygen/lib/utils/StateMachine.h>

namespace proxygen {

class HTTPTransactionEgressSMData {
 public:

  enum class State: uint8_t {
    Start,
    HeadersSent,
    RegularBodySent,
    ChunkHeaderSent,
    ChunkBodySent,
    ChunkTerminatorSent,
    TrailersSent,
    EOMQueued,
    SendingDone
  };

  enum class Event: uint8_t {
    // API accessible transitions
    sendHeaders,
    sendBody,
    sendChunkHeader,
    sendChunkTerminator,
    sendTrailers,
    sendEOM,
    // Internal state transitions
    eomFlushed,
  };

  static State getInitialState() {
    return State::Start;
  }

  static std::pair<State, bool> find(State s, Event e);
};

std::ostream& operator<<(std::ostream& os,
                         HTTPTransactionEgressSMData::State s);

std::ostream& operator<<(std::ostream& os,
                         HTTPTransactionEgressSMData::Event e);

typedef StateMachine<HTTPTransactionEgressSMData> HTTPTransactionEgressSM;

}
