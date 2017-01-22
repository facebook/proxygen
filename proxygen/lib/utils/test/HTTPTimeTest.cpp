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
#include <proxygen/lib/utils/HTTPTime.h>

using proxygen::parseHTTPDateTime;

TEST(HTTPTimeTests, InvalidTimeTest) {
  EXPECT_FALSE(parseHTTPDateTime("Hello, World").hasValue());
  EXPECT_FALSE(parseHTTPDateTime("Sun, 33 Nov 1994 08:49:37 GMT").hasValue());
  EXPECT_FALSE(parseHTTPDateTime("Sun, 06 Nov 1800").hasValue());
}

TEST(HTTPTimeTests, ValidTimeTest) {
  // From http://www.w3.org/Protocols/rfc2616/rfc2616-sec3.html#sec3.3
  EXPECT_TRUE(parseHTTPDateTime("Sun, 06 Nov 1994 08:49:37 GMT").hasValue());
  EXPECT_TRUE(parseHTTPDateTime("Sunday, 06-Nov-94 08:49:37 GMT").hasValue());
  EXPECT_TRUE(parseHTTPDateTime("Sun Nov  6 08:49:37 1994").hasValue());
}

TEST(HTTPTimeTests, EqualTimeTest) {
  auto a = parseHTTPDateTime("Thu, 07 Mar 2013 08:49:37 GMT");
  EXPECT_TRUE(a.hasValue());
  auto b = parseHTTPDateTime("Thursday, 07-Mar-13 08:49:37 GMT");
  EXPECT_TRUE(b.hasValue());
  auto c = parseHTTPDateTime("Thu Mar 7 08:49:37 2013");
  EXPECT_TRUE(c.hasValue());

  EXPECT_EQ(a.value(), b.value());
  EXPECT_EQ(a.value(), c.value());
  EXPECT_EQ(b.value(), c.value());
}

TEST(HTTPTimeTests, ReallyOldTimeTest) {
  auto a = parseHTTPDateTime("Thu, 07 Mar 1770 08:49:37 GMT");
  EXPECT_TRUE(a.hasValue());
  auto b = parseHTTPDateTime("Thu, 07 Mar 1771 08:49:37 GMT");
  EXPECT_TRUE(b.hasValue());
  auto c = parseHTTPDateTime("Thu, 07 Mar 1980 08:49:37 GMT");
  EXPECT_TRUE(c.hasValue());

  EXPECT_LT(a, b);
  EXPECT_LT(a, c);
  EXPECT_LT(b, c);
}
