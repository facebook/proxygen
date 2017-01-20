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

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <string>

#include <folly/portability/Time.h>

#include <openssl/ossl_typ.h>

namespace proxygen {

using SteadyClock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;
using TimePoint = SteadyClock::time_point;
using SystemTimePoint = SystemClock::time_point;

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

template <typename ClockType = SteadyClock>
inline std::chrono::time_point<ClockType> getCurrentTime() {
  return ClockType::now();
}

inline std::chrono::system_clock::time_point
toSystemTimePoint(TimePoint t) {
  return std::chrono::system_clock::now() +
    std::chrono::duration_cast<std::chrono::system_clock::duration>(
      t - SteadyClock::now());
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

template <typename ClockType = SteadyClock>
inline std::chrono::milliseconds millisecondsBetween(
    std::chrono::time_point<ClockType> finish,
    std::chrono::time_point<ClockType> start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    finish - start);
}

template <typename ClockType = SteadyClock>
inline std::chrono::seconds secondsBetween(
    std::chrono::time_point<ClockType> finish,
    std::chrono::time_point<ClockType> start) {
  return std::chrono::duration_cast<std::chrono::seconds>(
    finish - start);
}

template <typename ClockType = SteadyClock>
inline std::chrono::milliseconds millisecondsSince(
    std::chrono::time_point<ClockType> t) {
  return millisecondsBetween(getCurrentTime<ClockType>(), t);
}

template <typename ClockType = SteadyClock>
inline std::chrono::seconds secondsSince(std::chrono::time_point<ClockType> t) {
  return secondsBetween(getCurrentTime<ClockType>(), t);
}

/**
 * Get the current date and time in string formats: %Y-%m-%d and %H:%M:%S.
 */
inline void getDateTimeStr(char datebuf[32], char timebuf[32]) {
  time_t now = toTimeT(getCurrentTime<SteadyClock>());
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
 * Get the current date + offset days in %Y-%m-%d format.
 */
inline void getDateOffsetStr(char datebuf[32], int dayOffset) {
  time_t t = toTimeT(getCurrentTime<SteadyClock>());
  t += dayOffset * 24 * 60 * 60;
  struct tm final_tm;
  localtime_r(&t, &final_tm);
  strftime(datebuf, sizeof(char) * 32, "%Y-%m-%d", &final_tm);
}

/**
 * Helper method to convert to OpenSSL type ASN1_TIME to a printable date and
 * time string.
 *
 * @param time    a pointer to the ASN1_TIME instance to be converted.
 * @return        a human readable date and time string for the openssl type
 *                ASN1_TIME. If there is any error, returns empty string.
 */
std::string getDateTimeStr(const ASN1_TIME* const time);

/**
 * Class used to get steady time. We use a separate class to mock it easier.
 */
template <typename ClockType = SteadyClock>
class TimeUtilGeneric {
 public:
  virtual ~TimeUtilGeneric() {}

  virtual std::chrono::time_point<ClockType> now() const {
    return getCurrentTime<ClockType>();
  }

  static const std::chrono::time_point<ClockType>& getZeroTimePoint() {
    const static std::chrono::time_point<ClockType> kZeroTimePoint{};
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

// Typedef so as to not disrupting callers who use 'TimeUtil' before we
// made it TimeUtilGeneric
using TimeUtil = TimeUtilGeneric<>;

}
