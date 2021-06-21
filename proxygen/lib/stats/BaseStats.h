/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "fb303/ThreadCachedServiceData.h"

namespace proxygen {

/*
 * Counter definitions for use in child classes.  Updating all
 * children thus becomes as simple as updating these definitions.
 * It is thus intended and recommended for all callers to refer to
 * BaseStats::<counter> when wishing to use counters.
 */
class BaseStats {
 private:
  // Private constructor so its clear nothing else should implement this class
  BaseStats() = default;

 public:
  // TODO: given the simple nature of TLCounter and that it is explicitly
  // thread safe via the use of atomics, we may only want single local
  // instance instead of wrapped (per thread) instances.
  using TLCounter = facebook::fb303::CounterWrapper;
  using TLDynamicTimeseries = facebook::fb303::DynamicTimeseriesWrapper<1>;
  using TLTimeseries = facebook::fb303::TimeseriesPolymorphicWrapper;
  using TLTimeseriesQuarterMinuteOnly =
      facebook::fb303::QuarterMinuteOnlyTimeseriesWrapper;
  using TLTimeseriesMinute = facebook::fb303::MinuteOnlyTimeseriesWrapper;
  using TLTimeseriesMinuteAndAllTime = facebook::fb303::MinuteTimeseriesWrapper;
  using TLHistogram = facebook::fb303::HistogramWrapper;
};

} // namespace proxygen
