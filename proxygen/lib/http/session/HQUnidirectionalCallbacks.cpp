/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <proxygen/lib/http/session/HQUnidirectionalCallbacks.h>

using namespace proxygen;

HQUnidirStreamDispatcher::HQUnidirStreamDispatcher(
    HQUnidirStreamDispatcher::Callback& sink)
    : controlStreamCallback_(std::make_unique<ControlCallback>(sink)),
      sink_(sink) {
}

void HQUnidirStreamDispatcher::onDataAvailable(
    quic::StreamId id, const Callback::PeekData& peekData) noexcept {
  if (peekData.empty()) {
    return;
  }

  // If this strem is operating in the partially reliable mode,
  // do not attempt to parse the preface.
  // The sink is responsible for deciding when a stream can become
  // partially reliable.
  if (sink_.isPartialReliabilityEnabled(id)) {
    sink_.onPartialDataAvailable(id, peekData);
    return;
  }

  // if not at offset 0, ignore
  if (peekData.front().offset != 0) {
    return;
  }

  // Look for a stream preface in the first read buffer
  folly::io::Cursor cursor(peekData.front().data.front());
  auto preface = quic::decodeQuicInteger(cursor);
  if (!preface) {
    return;
  }

  auto consumed = preface->second;
  auto type = sink_.parseStreamPreface(preface->first);

  if (!type) {
    // Failed to identify the preface, signal error
    sink_.rejectStream(id);
    return;
  }

  switch (type.value()) {
    case hq::UnidirectionalStreamType::H1Q_CONTROL:
    case hq::UnidirectionalStreamType::CONTROL:
    case hq::UnidirectionalStreamType::QPACK_ENCODER:
    case hq::UnidirectionalStreamType::QPACK_DECODER: {
      // This is a control stream, and it needs a read callback
      sink_.assignReadCallback(
          id, type.value(), consumed, controlStreamCallback());
      return;
    }
    case hq::UnidirectionalStreamType::PUSH: {
      // Try to read the push id from the stream
      auto pushId = quic::decodeQuicInteger(cursor);
      // If successfully read the push id, call sink
      // which will reassign the peek callback
      // Otherwise, continue using this callback
      if (pushId) {
        consumed += pushId->second;
        sink_.onNewPushStream(id, pushId->first, consumed);
      }
      return;
    }
    default: {
      LOG(ERROR) << "Unrecognized type=" << static_cast<uint64_t>(type.value());
    }
  }
}

void HQUnidirStreamDispatcher::onDataExpired(quic::StreamId id,
                                             uint64_t offset) noexcept {
  if (sink_.isPartialReliabilityEnabled(id)) {
    sink_.processExpiredData(id, offset);
  } else {
    VLOG(4) << __func__ << " streamID=" << id << " does not uspoort PR";
  }
}

void HQUnidirStreamDispatcher::onDataRejected(quic::StreamId id,
                                              uint64_t offset) noexcept {
  if (sink_.isPartialReliabilityEnabled(id)) {
    sink_.processRejectedData(id, offset);
  } else {
    VLOG(4) << __func__ << " streamID=" << id << " does not uspoort PR";
  }
}

quic::QuicSocket::ReadCallback*
HQUnidirStreamDispatcher::controlStreamCallback() const {
  return controlStreamCallback_.get();
}

// Control stream callback implementation
void HQUnidirStreamDispatcher::ControlCallback::readError(
    quic::StreamId id,
    HQUnidirStreamDispatcher::Callback::ReadError error) noexcept {
  sink_.controlStreamReadError(id, error);
}

void HQUnidirStreamDispatcher::ControlCallback::readAvailable(
    quic::StreamId id) noexcept {
  sink_.controlStreamReadAvailable(id);
}

