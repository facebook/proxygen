/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/observer/HTTPTransactionObserverInterface.h>

namespace proxygen {

HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange::Builder&&
HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange::Builder::
    setStreamId(uint64_t streamIdIn) {
  maybeStreamId = streamIdIn;
  return std::move(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange::Builder&&
HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange::Builder::
    setBodyOffset(uint64_t bodyOffsetIn) {
  maybeBodyOffset = bodyOffsetIn;
  return std::move(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange::Builder&&
HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange::Builder::
    setStreamOffset(uint64_t streamOffsetIn) {
  maybeStreamOffset = streamOffsetIn;
  return std::move(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange::Builder&&
HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange::Builder::
    setLength(size_t lengthIn) {
  maybeLength = lengthIn;
  return std::move(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange
HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange::Builder::
    build() && {
  return BodyByteStreamRange(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::BodyByteStreamRange::
    BodyByteStreamRange(BuilderFields& builderFields)
    : streamId(*CHECK_NOTNULL(builderFields.maybeStreamId.get_pointer())),
      bodyOffset(*CHECK_NOTNULL(builderFields.maybeBodyOffset.get_pointer())),
      streamOffset(
          *CHECK_NOTNULL(builderFields.maybeStreamOffset.get_pointer())),
      length(*CHECK_NOTNULL(builderFields.maybeLength.get_pointer())) {
}

HTTPTransactionObserverInterface::TxnBytesEvent::Builder&&
HTTPTransactionObserverInterface::TxnBytesEvent::Builder::setTimestamp(
    const proxygen::TimePoint& timestampIn) {
  maybeTimestampRef = timestampIn;
  return std::move(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::Builder&&
HTTPTransactionObserverInterface::TxnBytesEvent::Builder::setType(Type typeIn) {
  type = typeIn;
  return std::move(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::Builder&&
HTTPTransactionObserverInterface::TxnBytesEvent::Builder::setNumBytes(
    const size_t numBytesIn) {
  numBytes = numBytesIn;
  return std::move(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::Builder&&
HTTPTransactionObserverInterface::TxnBytesEvent::Builder::setNumBodyBytes(
    size_t numBodyBytesIn) {
  numBodyBytes = numBodyBytesIn;
  return std::move(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::Builder&&
HTTPTransactionObserverInterface::TxnBytesEvent::Builder::setHeaders(
    const HTTPMessage& headersIn) {
  maybeHeadersRef = headersIn;
  return std::move(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::Builder&&
HTTPTransactionObserverInterface::TxnBytesEvent::Builder::
    setBodyByteStreamRange(const BodyByteStreamRange& bodyByteStreamRangeIn) {
  maybeBodyByteStreamRange = bodyByteStreamRangeIn;
  return std::move(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent
HTTPTransactionObserverInterface::TxnBytesEvent::Builder::build() && {
  return TxnBytesEvent(*this);
}

HTTPTransactionObserverInterface::TxnBytesEvent::TxnBytesEvent(
    const TxnBytesEvent::BuilderFields& builderFields)
    : timestamp(*CHECK_NOTNULL(builderFields.maybeTimestampRef.get_pointer())),
      type(builderFields.type),
      maybeNumBytes(builderFields.numBytes),
      maybeNumBodyBytes(builderFields.numBodyBytes),
      maybeHeadersRef(builderFields.maybeHeadersRef),
      maybeBodyByteStreamRange(builderFields.maybeBodyByteStreamRange) {
}

} // namespace proxygen
