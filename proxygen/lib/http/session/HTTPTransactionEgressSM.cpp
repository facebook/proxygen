/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPTransactionEgressSM.h>

#include <proxygen/lib/utils/UnionBasedStatic.h>

namespace proxygen {

namespace {

typedef typename HTTPTransactionEgressSMData::State State;
typedef typename HTTPTransactionEgressSMData::Event Event;
typedef std::map<std::pair<State, Event>, State> TransitionTable;

DEFINE_UNION_STATIC_CONST_NO_INIT(TransitionTable, Table, s_transitions);

//             +--> ChunkHeaderSent -> ChunkBodySent
//             |      ^                    v
//             |      |   ChunkTerminatorSent -> TrailersSent
//             |      |__________|        |          |
//             |                          |          v
// Start -> HeadersSent                   +----> EOMQueued --> SendingDone
//             |                                     ^
//             +------------> RegularBodySent -------+
__attribute__((__constructor__))
void initTableUnion() {
  // Use const_cast for the placement new initialization since we want this to
  // be const after construction.
  new (const_cast<TransitionTable*>(&s_transitions.data)) TransitionTable {
    {{State::Start, Event::sendHeaders}, State::HeadersSent},

    // For HTTP sending 100 response, then a regular response
    {{State::HeadersSent, Event::sendHeaders}, State::HeadersSent},

    {{State::HeadersSent, Event::sendBody}, State::RegularBodySent},
    {{State::HeadersSent, Event::sendChunkHeader}, State::ChunkHeaderSent},
    {{State::HeadersSent, Event::sendEOM}, State::EOMQueued},

    {{State::RegularBodySent, Event::sendBody}, State::RegularBodySent},
    {{State::RegularBodySent, Event::sendEOM}, State::EOMQueued},

    {{State::ChunkHeaderSent, Event::sendBody}, State::ChunkBodySent},

    {{State::ChunkBodySent, Event::sendBody}, State::ChunkBodySent},
    {{State::ChunkBodySent, Event::sendChunkTerminator},
     State::ChunkTerminatorSent},

    {{State::ChunkTerminatorSent, Event::sendChunkHeader},
     State::ChunkHeaderSent},
    {{State::ChunkTerminatorSent, Event::sendTrailers}, State::TrailersSent},
    {{State::ChunkTerminatorSent, Event::sendEOM}, State::EOMQueued},

    {{State::TrailersSent, Event::sendEOM}, State::EOMQueued},

    {{State::EOMQueued, Event::eomFlushed}, State::SendingDone},
  };
}

} // namespace

std::pair<HTTPTransactionEgressSMData::State, bool>
HTTPTransactionEgressSMData::find(HTTPTransactionEgressSMData::State s,
                                  HTTPTransactionEgressSMData::Event e) {
  auto const &it = s_transitions.data.find(std::make_pair(s, e));
  if (it == s_transitions.data.end()) {
    return std::make_pair(s, false);
  }

  return std::make_pair(it->second, true);
}

std::ostream& operator<<(std::ostream& os,
                         HTTPTransactionEgressSMData::State s) {
  switch (s) {
    case HTTPTransactionEgressSMData::State::Start:
      os << "Start";
      break;
    case HTTPTransactionEgressSMData::State::HeadersSent:
      os << "HeadersSent";
      break;
    case HTTPTransactionEgressSMData::State::RegularBodySent:
      os << "RegularBodySent";
      break;
    case HTTPTransactionEgressSMData::State::ChunkHeaderSent:
      os << "ChunkHeaderSent";
      break;
    case HTTPTransactionEgressSMData::State::ChunkBodySent:
      os << "ChunkBodySent";
      break;
    case HTTPTransactionEgressSMData::State::ChunkTerminatorSent:
      os << "ChunkTerminatorSent";
      break;
    case HTTPTransactionEgressSMData::State::TrailersSent:
      os << "TrailersSent";
      break;
    case HTTPTransactionEgressSMData::State::EOMQueued:
      os << "EOMQueued";
      break;
    case HTTPTransactionEgressSMData::State::SendingDone:
      os << "SendingDone";
      break;
  }

  return os;
}

std::ostream& operator<<(std::ostream& os,
                         HTTPTransactionEgressSMData::Event e) {
  switch (e) {
    case HTTPTransactionEgressSMData::Event::sendHeaders:
      os << "sendHeaders";
      break;
    case HTTPTransactionEgressSMData::Event::sendBody:
      os << "sendBody";
      break;
    case HTTPTransactionEgressSMData::Event::sendChunkHeader:
      os << "sendChunkHeader";
      break;
    case HTTPTransactionEgressSMData::Event::sendChunkTerminator:
      os << "sendChunkTerminator";
      break;
    case HTTPTransactionEgressSMData::Event::sendTrailers:
      os << "sendTrailers";
      break;
    case HTTPTransactionEgressSMData::Event::sendEOM:
      os << "sendEOM";
      break;
    case HTTPTransactionEgressSMData::Event::eomFlushed:
      os << "eomFlushed";
      break;
  }

  return os;
}

}
