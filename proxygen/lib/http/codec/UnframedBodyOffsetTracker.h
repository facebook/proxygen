/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Expected.h>
#include <folly/Optional.h>

namespace proxygen { namespace hq {

enum class UnframedBodyOffsetTrackerError {
  NO_ERROR = 0,
  START_OFFSET_NOT_SET,
  START_OFFSET_ALREADY_SET,
  INVALID_OFFSET
};

std::string toString(UnframedBodyOffsetTrackerError error);
std::ostream& operator<<(std::ostream& os,
                         const UnframedBodyOffsetTrackerError& error);

using TrackerOffsetResult =
    folly::Expected<uint64_t, UnframedBodyOffsetTrackerError>;

/**
 * Class to track and operate on partially reliable body offsets:
 *  - keeps track of where the body starts on the stream
 *  - translates offsets between body (application) and stream values which are
 *    different with partially reliable extensions
 */
class UnframedBodyOffsetTracker {
 public:
  ~UnframedBodyOffsetTracker() = default;
  explicit UnframedBodyOffsetTracker() {
  }

  folly::Expected<folly::Unit, UnframedBodyOffsetTrackerError>
  startBodyTracking(uint64_t streamOffset);

  bool bodyStarted() const;

  /**
   * Increments body bytes processed.
   */
  void addBodyBytesProcessed(uint64_t n);

  /**
   * Pushes body bytes processed forward if new offset is larger than already
   * registered offset.
   */
  bool maybeMoveBodyBytesProcessed(uint64_t appSkipOffset);

  /**
   * Returns body bytes processed so far.
   */
  uint64_t getBodyBytesProcessed() const;

  /**
   * Returns body stream start offset.
   */
  TrackerOffsetResult getBodyStreamStartOffset() const;

  /**
   * Translates body offset given into stream offset.
   */
  TrackerOffsetResult appTostreamOffset(uint64_t bodyOffset) const;

  /**
   * Translates stream offset given into body offset.
   */
  TrackerOffsetResult streamToBodyOffset(uint64_t streamOffset) const;

 private:
  /**
   * Where body starts in transport stream offset.
   *
   * The value should always be larger than zero, because HTTP doesn't allow
   * sending body bytes the very first thing on the stream. Body should be
   * always preceded by headers, hence non-zero offset.
   */
  folly::Optional<uint64_t> bodyStartstreamOffset_;

  /**
   * Where we stand in terms of body bytes sent or received. This includes both
   * bytes sent/received and bytes expired/rejected.
   *
   * This is an application offset.
   *
   * The value always starts with 0 (e.g. we have 0 bytes sent or received in
   * the beginning). And then grows all the up to the number of body bytes
   * sent/received + skipped/rejected.
   */
  uint64_t appBodyBytesProcessed_{0};
};
}} // namespace proxygen::hq
