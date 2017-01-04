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

#include <bitset>
#include <boost/optional/optional.hpp>
#include <deque>
#include <proxygen/lib/http/HTTPHeaders.h>
#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/http/codec/HTTPSettings.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <zlib.h>

namespace folly { namespace io {
class Cursor;
}}

namespace proxygen {

/**
 * An implementation of common codec functionality used for multiple
 * parallel stream downloads. Currently shared by SPDY and HTTP/2
 */
class HTTPParallelCodec: public HTTPCodec {
public:
  explicit HTTPParallelCodec(TransportDirection direction);

  TransportDirection getTransportDirection() const override {
    return transportDirection_;
  }

  StreamID createStream() override;
  bool isBusy() const override { return false; }
  bool supportsStreamFlowControl() const override { return true; }
  bool supportsSessionFlowControl() const override { return true; }
  bool supportsParallelRequests() const override { return true; }
  bool closeOnEgressComplete() const override { return false; }
  void setCallback(Callback* callback) override { callback_ = callback; }
  void setParserPaused(bool /* paused */) override {}
  void onIngressEOF() override {}
  bool isReusable() const override;
  bool isWaitingToDrain() const override;
  StreamID getLastIncomingStreamID() const override { return lastStreamID_; }
  void enableDoubleGoawayDrain() override;

  bool onIngressUpgradeMessage(const HTTPMessage& msg) override;

  void setNextEgressStreamId(StreamID nextEgressStreamID) {
    if (nextEgressStreamID > nextEgressStreamID_ &&
        (nextEgressStreamID & 0x1) == (nextEgressStreamID_ & 0x1) &&
        nextEgressStreamID_ < std::numeric_limits<int32_t>::max()) {
      nextEgressStreamID_ = nextEgressStreamID;
    }
  }

  bool isInitiatedStream(StreamID stream) const {
    bool odd = stream & 0x01;
    bool upstream = (transportDirection_ == TransportDirection::UPSTREAM);
    return (odd && upstream) || (!odd && !upstream);
  }

  bool isStreamIngressEgressAllowed(StreamID stream) const {
    bool isInitiated = isInitiatedStream(stream);
    return (isInitiated && stream <= ingressGoawayAck_) ||
      (!isInitiated && stream <= egressGoawayAck_);
  }

protected:
  TransportDirection transportDirection_;
  StreamID nextEgressStreamID_;
  StreamID lastStreamID_{0};
  HTTPCodec::Callback* callback_{nullptr};
  StreamID ingressGoawayAck_{std::numeric_limits<uint32_t>::max()};
  StreamID egressGoawayAck_{std::numeric_limits<uint32_t>::max()};
  std::string goawayErrorMessage_;

  enum ClosingState {
    OPEN = 0,
    OPEN_WITH_GRACEFUL_DRAIN_ENABLED = 1,
    FIRST_GOAWAY_SENT = 2,
    CLOSING = 3, // SPDY only
    CLOSED = 4 // HTTP2 only
  } sessionClosing_;

  template<typename T, typename... Args>
  bool deliverCallbackIfAllowed(T callbackFn, char const* cbName,
                                StreamID stream, Args&&... args) {
    if (isStreamIngressEgressAllowed(stream)) {
      if (callback_) {
        (*callback_.*callbackFn)(stream, std::forward<Args>(args)...);
      }
      return true;
    } else {
      VLOG(2) << "Suppressing " << cbName << " for stream=" <<
        stream << " egressGoawayAck_=" << egressGoawayAck_;
    }
    return false;
  }


};
} // proxygen
