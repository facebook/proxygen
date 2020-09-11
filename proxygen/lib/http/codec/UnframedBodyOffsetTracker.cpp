/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <ostream>
#include <string>

#include <proxygen/lib/http/codec/UnframedBodyOffsetTracker.h>

namespace proxygen { namespace hq {

folly::Expected<folly::Unit, UnframedBodyOffsetTrackerError>
UnframedBodyOffsetTracker::startBodyTracking(uint64_t streamOffset) {
  if (bodyStartstreamOffset_) {
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::START_OFFSET_ALREADY_SET);
  }
  bodyStartstreamOffset_ = streamOffset;
  return folly::makeExpected<UnframedBodyOffsetTrackerError>(folly::Unit());
}

bool UnframedBodyOffsetTracker::bodyStarted() const {
  return bodyStartstreamOffset_.has_value();
}

void UnframedBodyOffsetTracker::addBodyBytesProcessed(uint64_t n) {
  appBodyBytesProcessed_ += n;
}

bool UnframedBodyOffsetTracker::maybeMoveBodyBytesProcessed(
    uint64_t appSkipOffset) {
  if (appSkipOffset <= appBodyBytesProcessed_) {
    return false;
  }
  appBodyBytesProcessed_ = appSkipOffset;

  return true;
}

uint64_t UnframedBodyOffsetTracker::getBodyBytesProcessed() const {
  return appBodyBytesProcessed_;
}

TrackerOffsetResult UnframedBodyOffsetTracker::getBodyStreamStartOffset()
    const {
  if (!bodyStartstreamOffset_) {
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::START_OFFSET_NOT_SET);
  }
  return *bodyStartstreamOffset_;
}

TrackerOffsetResult UnframedBodyOffsetTracker::appTostreamOffset(
    uint64_t bodyOffset) const {
  if (!bodyStartstreamOffset_) {
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::START_OFFSET_NOT_SET);
  }
  return *bodyStartstreamOffset_ + bodyOffset;
}

TrackerOffsetResult UnframedBodyOffsetTracker::streamToBodyOffset(
    uint64_t streamOffset) const {
  if (!bodyStartstreamOffset_) {
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::START_OFFSET_NOT_SET);
  }

  if (streamOffset < *bodyStartstreamOffset_) {
    return folly::makeUnexpected(
        UnframedBodyOffsetTrackerError::INVALID_OFFSET);
  }

  return streamOffset - *bodyStartstreamOffset_;
}

std::string toString(UnframedBodyOffsetTrackerError error) {
  switch (error) {
    case UnframedBodyOffsetTrackerError::NO_ERROR:
      return "no error";
    case UnframedBodyOffsetTrackerError::START_OFFSET_NOT_SET:
      return "body start offset not set";
    case UnframedBodyOffsetTrackerError::START_OFFSET_ALREADY_SET:
      return "body start offset already set";
    case UnframedBodyOffsetTrackerError::INVALID_OFFSET:
      return "invalid offset";
  }
  return "Unknown error";
}

std::ostream& operator<<(std::ostream& os,
                         const UnframedBodyOffsetTrackerError& error) {
  os << toString(error);
  return os;
}

}} // namespace proxygen::hq
