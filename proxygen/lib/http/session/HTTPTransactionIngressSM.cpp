/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPTransactionIngressSM.h>

#include <proxygen/lib/utils/UnionBasedStatic.h>

namespace proxygen {

namespace {

typedef typename HTTPTransactionIngressSMData::State State;
typedef typename HTTPTransactionIngressSMData::Event Event;
typedef std::map<std::pair<State, Event>, State> TransitionTable;

DEFINE_UNION_STATIC_CONST_NO_INIT(TransitionTable, Table, s_transitions);

//             +--> ChunkHeaderReceived -> ChunkBodyReceived
//             |        ^                     v
//             |        |          ChunkCompleted -> TrailersReceived
//             |        |_______________|     |      |
//             |                              v      v
// Start -> HeadersReceived ---------------> EOMQueued ---> ReceivingDone
//             |  |                             ^  ^
//             |  +-----> RegularBodyReceived --+  |
//             |                                   |
//             +---------> UpgradeComplete --------+
__attribute__((__constructor__))
void initTableUnion() {
  // Use const_cast for the placement new initialization since we want this to
  // be const after construction.
  new (const_cast<TransitionTable*>(&s_transitions.data)) TransitionTable {
    {{State::Start, Event::onHeaders}, State::HeadersReceived},

    // For HTTP receiving 100 response, then a regular response
    {{State::HeadersReceived, Event::onHeaders}, State::HeadersReceived},

    {{State::HeadersReceived, Event::onBody}, State::RegularBodyReceived},
    {{State::HeadersReceived, Event::onChunkHeader},
     State::ChunkHeaderReceived},
    // special case - 0 byte body with trailers
    {{State::HeadersReceived, Event::onTrailers}, State::TrailersReceived},
    {{State::HeadersReceived, Event::onUpgrade}, State::UpgradeComplete},
    {{State::HeadersReceived, Event::onEOM}, State::EOMQueued},

    {{State::RegularBodyReceived, Event::onBody}, State::RegularBodyReceived},
    {{State::RegularBodyReceived, Event::onEOM}, State::EOMQueued},

    {{State::ChunkHeaderReceived, Event::onBody}, State::ChunkBodyReceived},

    {{State::ChunkBodyReceived, Event::onBody}, State::ChunkBodyReceived},
    {{State::ChunkBodyReceived, Event::onChunkComplete}, State::ChunkCompleted},

    {{State::ChunkCompleted, Event::onChunkHeader}, State::ChunkHeaderReceived},
    // TODO: "trailers" may be received at any time due to the SPDY HEADERS
    // frame coming at any time. We might want to have a
    // TransactionStateMachineFactory that takes a codec and generates the
    // appropriate transaction state machine from that.
    {{State::ChunkCompleted, Event::onTrailers}, State::TrailersReceived},
    {{State::ChunkCompleted, Event::onEOM}, State::EOMQueued},

    {{State::TrailersReceived, Event::onEOM}, State::EOMQueued},

    {{State::UpgradeComplete, Event::onBody}, State::UpgradeComplete},
    {{State::UpgradeComplete, Event::onEOM}, State::EOMQueued},

    {{State::EOMQueued, Event::eomFlushed}, State::ReceivingDone},
  };
}

} // namespace

std::pair<HTTPTransactionIngressSMData::State, bool>
HTTPTransactionIngressSMData::find(HTTPTransactionIngressSMData::State s,
                                   HTTPTransactionIngressSMData::Event e) {
  auto const &it = s_transitions.data.find(std::make_pair(s, e));
  if (it == s_transitions.data.end()) {
    return std::make_pair(s, false);
  }

  return std::make_pair(it->second, true);
}

std::ostream& operator<<(std::ostream& os,
                         HTTPTransactionIngressSMData::State s) {
  switch (s) {
    case HTTPTransactionIngressSMData::State::Start:
      os << "Start";
      break;
    case HTTPTransactionIngressSMData::State::HeadersReceived:
      os << "HeadersReceived";
      break;
    case HTTPTransactionIngressSMData::State::RegularBodyReceived:
      os << "RegularBodyReceived";
      break;
    case HTTPTransactionIngressSMData::State::ChunkHeaderReceived:
      os << "ChunkHeaderReceived";
      break;
    case HTTPTransactionIngressSMData::State::ChunkBodyReceived:
      os << "ChunkBodyReceived";
      break;
    case HTTPTransactionIngressSMData::State::ChunkCompleted:
      os << "ChunkCompleted";
      break;
    case HTTPTransactionIngressSMData::State::TrailersReceived:
      os << "TrailersReceived";
      break;
    case HTTPTransactionIngressSMData::State::UpgradeComplete:
      os << "UpgradeComplete";
      break;
    case HTTPTransactionIngressSMData::State::EOMQueued:
      os << "EOMQueued";
      break;
    case HTTPTransactionIngressSMData::State::ReceivingDone:
      os << "ReceivingDone";
      break;
  }

  return os;
}

std::ostream& operator<<(std::ostream& os,
                         HTTPTransactionIngressSMData::Event e) {
  switch (e) {
    case HTTPTransactionIngressSMData::Event::onHeaders:
      os << "onHeaders";
      break;
    case HTTPTransactionIngressSMData::Event::onBody:
      os << "onBody";
      break;
    case HTTPTransactionIngressSMData::Event::onChunkHeader:
      os << "onChunkHeader";
      break;
    case HTTPTransactionIngressSMData::Event::onChunkComplete:
      os << "onChunkComplete";
      break;
    case HTTPTransactionIngressSMData::Event::onTrailers:
      os << "onTrailers";
      break;
    case HTTPTransactionIngressSMData::Event::onUpgrade:
      os << "onUpgrade";
      break;
    case HTTPTransactionIngressSMData::Event::onEOM:
      os << "onEOM";
      break;
    case HTTPTransactionIngressSMData::Event::eomFlushed:
      os << "eomFlushed";
      break;
  }

  return os;
}

}
