/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "proxygen/lib/stats/BaseStats.h"

namespace proxygen {

struct ResponseCodeStatsMinute {
  explicit ResponseCodeStatsMinute(const std::string& name);

  void addStatus(int status);

  BaseStats::TLTimeseriesMinute statusOther;
  BaseStats::TLTimeseriesMinute status1xx;
  BaseStats::TLTimeseriesMinute status2xx;
  BaseStats::TLTimeseriesMinute status3xx;
  BaseStats::TLTimeseriesMinute status4xx;
  BaseStats::TLTimeseriesMinute status5xx;
};

} // namespace proxygen
