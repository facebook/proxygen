/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/Window.h>

using namespace proxygen;

TEST(WindowTest, basic) {
  Window w(100);
  ASSERT_TRUE(w.free(10));
  ASSERT_EQ(w.getSize(), 110);
  ASSERT_EQ(w.getCapacity(), 100);
  ASSERT_TRUE(w.reserve(20));
  ASSERT_EQ(w.getSize(), 90);
  ASSERT_TRUE(w.reserve(90));
  ASSERT_EQ(w.getSize(), 0);
  ASSERT_TRUE(w.free(5));
  ASSERT_EQ(w.getSize(), 5);
  ASSERT_TRUE(w.free(0));
  ASSERT_EQ(w.getSize(), 5);
}

TEST(WindowTest, change_capacity) {
  Window w(100);
  ASSERT_TRUE(w.setCapacity(10));
  ASSERT_EQ(w.getSize(), 10);
  ASSERT_EQ(w.getCapacity(), 10);
  ASSERT_TRUE(w.reserve(7));
  ASSERT_TRUE(w.setCapacity(5));
  ASSERT_EQ(w.getCapacity(), 5);
  ASSERT_EQ(w.getSize(), -2);
  ASSERT_TRUE(w.free(1));
  ASSERT_EQ(w.getSize(), -1);
  ASSERT_TRUE(w.setCapacity(6));
  ASSERT_EQ(w.getSize(), 0);
  ASSERT_EQ(w.getCapacity(), 6);
  ASSERT_TRUE(w.setCapacity(7));
  ASSERT_EQ(w.getSize(), 1);
  ASSERT_EQ(w.getCapacity(), 7);
  ASSERT_EQ(w.getOutstanding(), 6);
}

TEST(WindowTest, exceed_window) {
  Window w(100);
  ASSERT_TRUE(w.reserve(50));
  ASSERT_TRUE(w.reserve(40));
  ASSERT_EQ(w.getSize(), 10);
  ASSERT_FALSE(w.reserve(20));
}

TEST(WindowTest, overflow) {
  Window w(0);
  ASSERT_FALSE(w.reserve(std::numeric_limits<int32_t>::max()));
}

TEST(WindowTest, underflow) {
  Window w(100);
  ASSERT_TRUE(w.free(100)); // You can manually bump up the window
  ASSERT_TRUE(w.free(100)); // You can manually bump up the window
  ASSERT_EQ(w.getSize(), 300);
  ASSERT_FALSE(w.free(std::numeric_limits<int32_t>::max())); // to a point
}

TEST(WindowTest, huge_reserve) {
  Window w(100);
  ASSERT_FALSE(w.reserve(std::numeric_limits<uint32_t>::max()));
}

TEST(WindowTest, huge_free) {
  Window w1(0);
  ASSERT_TRUE(w1.free(std::numeric_limits<int32_t>::max()));
  Window w2(1);
  ASSERT_FALSE(w2.free(std::numeric_limits<int32_t>::max()));
}

TEST(WindowTest, huge_free_2) {
  for (unsigned i = 0; i < 10; ++i) {
    Window w(i);
    ASSERT_TRUE(w.free(std::numeric_limits<int32_t>::max() - i));
    ASSERT_FALSE(w.free(1));
  }
}

TEST(WindowTest, bytes_outstanding) {
  Window w(100);
  ASSERT_EQ(w.getOutstanding(), 0);
  ASSERT_TRUE(w.reserve(20));
  ASSERT_EQ(w.getOutstanding(), 20);
  ASSERT_TRUE(w.free(30));
  // outstanding bytes is -10, but the API clamps this to 0
  ASSERT_EQ(w.getOutstanding(), 0);
}

TEST(WindowTest, bytes_outstanding_after_fail) {
  Window w(100);
  ASSERT_EQ(w.getOutstanding(), 0);
  ASSERT_FALSE(w.reserve(110));
  ASSERT_EQ(w.getOutstanding(), 0);
}

TEST(WindowTest, non_strict) {
  Window w(100);
  ASSERT_TRUE(w.reserve(110, false));
  ASSERT_EQ(w.getOutstanding(), 110);
  ASSERT_EQ(w.getSize(), -10);
}

TEST(WindowTest, new_capacity_overflow) {
  Window w(0);
  ASSERT_TRUE(w.free(10));
  ASSERT_EQ(w.getOutstanding(), 0);
  ASSERT_EQ(w.getSize(), 10);
  ASSERT_TRUE(w.setCapacity(std::numeric_limits<int32_t>::max() - 10));
  ASSERT_EQ(w.getSize(), std::numeric_limits<int32_t>::max());
  ASSERT_FALSE(w.setCapacity(std::numeric_limits<int32_t>::max() - 9));
}
