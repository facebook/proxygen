/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPTransactionIngressSM.h>

#include <folly/Indestructible.h>

namespace proxygen {

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

std::pair<HTTPTransactionIngressSMData::State, bool>
HTTPTransactionIngressSMData::find(HTTPTransactionIngressSMData::State s,
                                   HTTPTransactionIngressSMData::Event e) {
  using State = HTTPTransactionIngressSMData::State;
  using Event = HTTPTransactionIngressSMData::Event;
  using TransitionTable = std::map<std::pair<State, Event>, State>;

  static const folly::Indestructible<TransitionTable> transitions{
    TransitionTable{
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
    }
  };

  auto const &it = transitions->find(std::make_pair(s, e));
  if (it == transitions->end()) {
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
