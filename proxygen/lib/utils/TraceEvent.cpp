/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/utils/TraceEvent.h>

#include <folly/DynamicConverter.h>
#include <folly/ThreadLocal.h>
#include <random>
#include <sstream>
#include <string>

namespace {

class TraceEventIDGenerator {
 public:
  TraceEventIDGenerator() {
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    static std::mt19937 generator;
    std::uniform_int_distribution<uint32_t> distribution;
    nextID_ = distribution(generator);
  }

  uint32_t nextID() {
    return ++nextID_;
  }

 private:
  uint32_t nextID_;
};

}

namespace proxygen {
TraceEvent::TraceEvent(TraceEventType type, uint32_t parentID):
  type_(type),
  parentID_(parentID) {
  static folly::ThreadLocal<TraceEventIDGenerator> idGenerator;
  id_ = idGenerator->nextID();
}

void TraceEvent::start(const TimeUtil& tm) {
  stateFlags_ |= State::STARTED;
  start_ = tm.now();
}

void TraceEvent::start(TimePoint startTime) {
  stateFlags_ |= State::STARTED;
  start_ = startTime;
}

void TraceEvent::end(const TimeUtil& tm) {
  stateFlags_ |= State::ENDED;
  end_ = tm.now();
}

void TraceEvent::end(TimePoint endTime) {
  stateFlags_ |= State::ENDED;
  end_ = endTime;
}

bool TraceEvent::hasStarted() const {
  return stateFlags_ & State::STARTED;
}

bool TraceEvent::hasEnded() const {
  return stateFlags_ & State::ENDED;
}

bool TraceEvent::addMeta(TraceFieldType key, folly::dynamic&& value) {
  auto rc = metaData_.emplace(key, value);

  // replace if key already exist
  if (!rc.second) {
    rc.first->second = value;
  }

  return rc.second;
}

bool TraceEvent::readBoolMeta(TraceFieldType key, bool& dest) const {
  if (metaData_.count(key)) {
    DCHECK(metaData_.at(key).isBool());
    dest = metaData_.at(key).asBool();
    return true;
  }
  return false;
}

bool TraceEvent::readStrMeta(TraceFieldType key, std::string& dest) const {
  if (metaData_.count(key)) {
    // no need to check if value is string type
    dest = metaData_.at(key).asString().toStdString();
    return true;
  }
  return false;
}

std::string TraceEvent::toString() const {
  std::ostringstream out;
  int startSinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(
    start_.time_since_epoch()).count();
  int endSinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(
    end_.time_since_epoch()).count();
  out << "TraceEvent(";
  out << "type='" << getTraceEventTypeString(type_) << "', ";
  out << "id='" << id_ << "', ";
  out << "parentID='" << parentID_ << "', ";
  out << "start='" << startSinceEpoch << "', ";
  out << "end='" << endSinceEpoch << "', ";
  out << "metaData='{";
  for (auto data : metaData_) {
    out << getTraceFieldTypeString(data.first) << ": "
        << folly::convertTo<std::string>(data.second) << ", ";
  }
  out << "}')";
  return out.str();
}

std::ostream& operator << (std::ostream& out, const TraceEvent& event) {
  out << event.toString();
  return out;
}
}
