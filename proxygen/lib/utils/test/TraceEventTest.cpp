/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/utils/Exception.h>
#include <proxygen/lib/utils/TraceEvent.h>
#include <proxygen/lib/utils/TraceEventType.h>
#include <proxygen/lib/utils/TraceFieldType.h>

#include <folly/portability/GTest.h>
#include <folly/portability/GMock.h>

#include <string>
#include <vector>

using namespace proxygen;

TEST(TraceEventTest, IntegralDataIntegralValue) {
  TraceEvent traceEvent( (TraceEventType::TotalRequest) );

  int64_t data(13);
  traceEvent.addMeta(TraceFieldType::Protocol, data);

  ASSERT_EQ(data,
      traceEvent.getTraceFieldDataAs<int64_t>(TraceFieldType::Protocol));
}

TEST(TraceEventTest, IntegralDataStringValue) {
  TraceEvent traceEvent( (TraceEventType::TotalRequest) );

  int64_t intData(13);
  traceEvent.addMeta(TraceFieldType::Protocol, intData);

  std::string strData(std::to_string(intData));

  ASSERT_EQ(strData,
      traceEvent.getTraceFieldDataAs<std::string>(TraceFieldType::Protocol));
}

TEST(TraceEventTest, IntegralDataVectorValue) {
  TraceEvent traceEvent( (TraceEventType::TotalRequest) );

  int64_t data(13);
  traceEvent.addMeta(TraceFieldType::Protocol, data);

  ASSERT_THROW(
      traceEvent.getTraceFieldDataAs<std::vector<std::string>>(
          TraceFieldType::Protocol),
      Exception);
}

TEST(TraceEventTest, StringDataIntegralValueConvertible) {
  TraceEvent traceEvent( (TraceEventType::TotalRequest) );

  int64_t intData(13);
  std::string strData(std::to_string(intData));
  traceEvent.addMeta(TraceFieldType::Protocol, strData);

  ASSERT_EQ(intData,
      traceEvent.getTraceFieldDataAs<int64_t>(TraceFieldType::Protocol));
}

TEST(TraceEventTest, StringDataIntegralValueNonConvertible) {
  TraceEvent traceEvent( (TraceEventType::TotalRequest) );

  std::string data("Abc");
  traceEvent.addMeta(TraceFieldType::Protocol, data);

  ASSERT_ANY_THROW(
      traceEvent.getTraceFieldDataAs<int64_t>(TraceFieldType::Protocol));
}

TEST(TraceEventTest, StringDataStringValue) {
  TraceEvent traceEvent( (TraceEventType::TotalRequest) );

  std::string data("Abc");
  traceEvent.addMeta(TraceFieldType::Protocol, data);

  ASSERT_EQ(data,
      traceEvent.getTraceFieldDataAs<std::string>(TraceFieldType::Protocol));
}


TEST(TraceEventTest, StringDataVectorValue) {
  TraceEvent traceEvent( (TraceEventType::TotalRequest) );

  std::string data("Abc");
  traceEvent.addMeta(TraceFieldType::Protocol, data);

  ASSERT_THROW(
      traceEvent.getTraceFieldDataAs<std::vector<std::string>>(
          TraceFieldType::Protocol),
      Exception);
}

TEST(TraceEventTest, VectorDataIntegralValue) {
  TraceEvent traceEvent( (TraceEventType::TotalRequest) );

  std::vector<std::string> data;
  data.push_back("Abc");
  data.push_back("Hij");
  data.push_back("Xyz");
  traceEvent.addMeta(TraceFieldType::Protocol, data);

  ASSERT_THROW(
      traceEvent.getTraceFieldDataAs<int64_t>(TraceFieldType::Protocol),
      Exception);
}

TEST(TraceEventTest, VectorDataStringValue) {
  TraceEvent traceEvent( (TraceEventType::TotalRequest) );

  std::vector<std::string> data;
  data.push_back("A");
  data.push_back("B");
  data.push_back("C");
  traceEvent.addMeta(TraceFieldType::Protocol, data);

  ASSERT_THROW(
      traceEvent.getTraceFieldDataAs<std::string>(TraceFieldType::Protocol),
      Exception);
}

TEST(TraceEventTest, VectorDataVectorValue) {
  TraceEvent traceEvent( (TraceEventType::TotalRequest) );

  std::vector<std::string> data;
  data.push_back("A");
  data.push_back("B");
  data.push_back("C");
  traceEvent.addMeta(TraceFieldType::Protocol, data);

  std::vector<std::string> extractedData(
      traceEvent.getTraceFieldDataAs<std::vector<std::string>>(
          TraceFieldType::Protocol));

  EXPECT_THAT(extractedData, testing::ContainerEq(data));
}
