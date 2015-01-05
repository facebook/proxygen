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

#include <folly/dynamic.h>
#include <proxygen/lib/utils/Time.h>
#include <proxygen/lib/utils/TraceEventType.h>
#include <proxygen/lib/utils/TraceFieldType.h>
#include <string>

namespace proxygen {

/**
 * Simple structure to track timming of event in request flow then we can
 * report back to the application.
 */
class TraceEvent {
 public:
  typedef std::map<TraceFieldType, folly::dynamic> MetaDataMap;

  explicit TraceEvent(TraceEventType type, uint32_t parentID = 0);

  /**
   * Sets the end time to the current time according to the TimeUtil.
   */
  void start(const TimeUtil& tm);

  /**
   * Sets the start time to the given TimePoint.
   */
  void start(TimePoint startTime);

  /**
   * Sets the end time to the current time according to the TimeUtil.
   */
  void end(const TimeUtil& tm);

  /**
   * Sets the end time to the given TimePoint.
   */
  void end(TimePoint endTime);

  /**
   * @Returns true iff start() has been called on this TraceEvent.
   */
  bool hasStarted() const;

  /**
   * @Returns true iff end() has been called on this TraceEvent.
   */
  bool hasEnded() const;

  TimePoint getStartTime() const {
    return start_;
  }

  TimePoint getEndTime() const {
    return end_;
  }

  TraceEventType getType() const {
    return type_;
  }

  uint32_t getID() const {
    return id_;
  }

  void setParentID(uint32_t parent) {
    parentID_ = parent;
  }

  uint32_t getParentID() const {
    return parentID_;
  }

  void setMetaData(MetaDataMap&& input) {
    metaData_ = input;
  }

  const MetaDataMap& getMetaData() const {
    return metaData_;
  }

  bool addMeta(TraceFieldType key, folly::dynamic&& value);

  template<typename T>
  bool increaseIntMeta(TraceFieldType key, const T delta) {
    T value = 0;
    readIntMeta(key, value);
    value += delta;
    return addMeta(key, value);
  };

  template<typename T>
  bool readIntMeta(TraceFieldType key, T& dest) const {
    if (getMetaData().count(key)) {
      DCHECK(getMetaData().at(key).isInt());
      dest = getMetaData().at(key).asInt();
      return true;
    }
    return false;
  };

  bool readBoolMeta(TraceFieldType key, bool& dest) const;

  bool readStrMeta(TraceFieldType key, std::string& dest) const;

  std::string toString() const;

  friend std::ostream& operator << (std::ostream& out,
                                    const TraceEvent& event);

 private:

  enum State {
    NOT_STARTED = 0,
    STARTED = 1,
    ENDED = 2,
  };

  uint8_t stateFlags_{0};
  TraceEventType type_;
  uint32_t id_;
  uint32_t parentID_;
  TimePoint start_;
  TimePoint end_;
  MetaDataMap metaData_;
};

}
