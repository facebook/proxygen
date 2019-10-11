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
