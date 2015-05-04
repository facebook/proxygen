/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/utils/TraceEvent.h>
#include <proxygen/lib/utils/UnionBasedStatic.h>

#include <folly/ThreadLocal.h>
#include <random>
#include <sstream>
#include <string>

namespace {

DEFINE_UNION_STATIC_UNION_IMPL(std::mutex, Mutex, s_mtx);
DEFINE_UNION_STATIC_UNION_IMPL(std::mt19937, Mt19937, s_generator);

class TraceEventIDGenerator {
 public:
  TraceEventIDGenerator() {
    std::lock_guard<std::mutex> lock(s_mtx.data);
    std::uniform_int_distribution<uint32_t> distribution;
    nextID_ = distribution(s_generator.data);
  }

  uint32_t nextID() {
    return ++nextID_;
  }

 private:
  uint32_t nextID_;
};

}

namespace proxygen {

DEFINE_UNION_STATIC_UNION_IMPL(folly::ThreadLocal<TraceEventIDGenerator>,
                    TraceEventIDGenerator,
                    s_idGenerator);

__attribute__((__constructor__))
void initIDGeneratorUnion() {
  new (&s_mtx.data) std::mutex();
  new (&s_generator.data) std::mt19937();
  new (&s_idGenerator.data) folly::ThreadLocal<TraceEventIDGenerator>();
}

TraceEvent::TraceEvent(TraceEventType type, uint32_t parentID):
  type_(type),
  id_(s_idGenerator.data->nextID()),
  parentID_(parentID) {
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

bool TraceEvent::readBoolMeta(TraceFieldType key, bool& dest) const {
  return readMeta(key, dest);
}

bool TraceEvent::readStrMeta(TraceFieldType key, std::string& dest) const {
  return readMeta(key, dest);
}
bool TraceEvent::addMetaInternal(TraceFieldType key, MetaData&& value) {
  auto rc = metaData_.emplace(key, value);

  // replace if key already exist
  if (!rc.second) {
    rc.first->second = value;
  }

  return rc.second;
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
  auto itr = getMetaDataItr();
  while (itr.isValid()) {
    out << getTraceFieldTypeString(itr.getKey()) << ": "
        << itr.getValueAs<std::string>() << ", ";
    itr.next();
  }
  out << "}')";
  return out.str();
}

std::ostream& operator << (std::ostream& out, const TraceEvent& event) {
  out << event.toString();
  return out;
}
}
