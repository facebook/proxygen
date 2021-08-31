/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/utils/URL.h>

#include <folly/portability/GTest.h>

using namespace proxygen;

TEST(URLTest, Root) {
  URL u("http", "www.facebook.com", 0);
  EXPECT_TRUE(u.isValid());
  EXPECT_EQ(u.getUrl(), "http://www.facebook.com/");
}

TEST(URLTest, CapitalSheme) {
  URL u("HTTPS", "www.facebook.com", 0);
  EXPECT_TRUE(u.isValid());
  EXPECT_EQ(u.getUrl(), "https://www.facebook.com/");
}

TEST(URLTest, Invalid) {
  URL u1("https://www.facebook.com/foo\xff", true, URL::Mode::STRICT);
  EXPECT_FALSE(u1.isValid());
  EXPECT_EQ(u1.getHost(), "");
  EXPECT_EQ(u1.getPath(), "");
  URL u2("https://www.facebook.com/foo\xff", true, URL::Mode::STRICT_COMPAT);
  EXPECT_TRUE(u2.isValid());
  EXPECT_EQ(u2.getHost(), "www.facebook.com");
  EXPECT_EQ(u2.getPath(), "/foo\xff");
}

TEST(URLTest, NonHTTPScheme) {
  URL u1("masque://www.facebook.com/foo", true, URL::Mode::STRICT);
  // Invalid, but host is still set
  EXPECT_FALSE(u1.isValid());
  EXPECT_EQ(u1.getHost(), "www.facebook.com");
  EXPECT_EQ(u1.getPath(), "/foo");
}
