/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <cinttypes>

namespace proxygen {

typedef std::chrono::steady_clock ClockType;
typedef ClockType::time_point TimePoint;

template <typename T>
bool durationInitialized(const T& duration) {
  static T zero(0);
  return duration != T::max() && duration >= zero;
}

template <typename T>
bool timePointInitialized(const T& time) {
  static T epoch;
  return time > epoch;
}

inline TimePoint getCurrentTime() {
  static_assert(ClockType::is_steady, "");
  return ClockType::now();
}

inline std::chrono::system_clock::time_point toSystemTimePoint(TimePoint t) {
  return std::chrono::system_clock::now() +
    std::chrono::duration_cast<std::chrono::system_clock::duration>(
      t - std::chrono::steady_clock::now());
}

inline time_t toTimeT(TimePoint t) {
  return std::chrono::system_clock::to_time_t(toSystemTimePoint(t));
}

inline std::chrono::milliseconds millisecondsSinceEpoch() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch());
}

inline std::chrono::seconds secondsSinceEpoch() {
  return std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::system_clock::now().time_since_epoch());
}

inline std::chrono::milliseconds millisecondsSinceEpoch(TimePoint t) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    toSystemTimePoint(t).time_since_epoch());
}

inline std::chrono::seconds secondsSinceEpoch(TimePoint t) {
  return std::chrono::duration_cast<std::chrono::seconds>(
    toSystemTimePoint(t).time_since_epoch());
}

inline std::chrono::milliseconds
millisecondsBetween(TimePoint finish, TimePoint start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    finish - start);
}

inline std::chrono::seconds
secondsBetween(TimePoint finish, TimePoint start) {
  return std::chrono::duration_cast<std::chrono::seconds>(
    finish - start);
}

inline std::chrono::milliseconds millisecondsSince(TimePoint t) {
  return millisecondsBetween(getCurrentTime(), t);
}

inline std::chrono::seconds secondsSince(TimePoint t) {
  return secondsBetween(getCurrentTime(), t);
}

/**
 * Get the current date and time in string formats: %Y-%m-%d and %H:%M:%S.
 */
inline void getDateTimeStr(char datebuf[32], char timebuf[32]) {
  time_t now = toTimeT(getCurrentTime());
  struct tm now_tm;
  localtime_r(&now, &now_tm);
  if (datebuf) {
    strftime(datebuf, sizeof(char) * 32, "%Y-%m-%d", &now_tm);
  }
  if (timebuf) {
    strftime(timebuf, sizeof(char) * 32, "%H:%M:%S", &now_tm);
  }
}

/**
 * Class used to get steady time. We use a separate class to mock it easier.
 */
class TimeUtil {
 public:
  virtual ~TimeUtil() {}

  virtual TimePoint now() const {
    return getCurrentTime();
  }

  static const TimePoint& getZeroTimePoint() {
    const static TimePoint kZeroTimePoint{};
    return kZeroTimePoint;
  }

  /**
   * Please use strongly typed time_point. This is for avoiding the copy and
   * garbage collection of time_point in Lua.
   */
  virtual uint64_t msSinceEpoch() {
    return millisecondsSinceEpoch().count();
  }
};

}
