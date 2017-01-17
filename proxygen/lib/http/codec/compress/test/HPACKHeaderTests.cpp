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
#include <proxygen/lib/http/codec/compress/HPACKHeader.h>
#include <sstream>

using namespace proxygen;
using namespace std;
using namespace testing;

class HPACKHeaderTests : public testing::Test {
};

TEST_F(HPACKHeaderTests, size) {
  HPACKHeader h(":path", "/");
  EXPECT_EQ(h.bytes(), 32 + 5 + 1);
}

TEST_F(HPACKHeaderTests, operators) {
  HPACKHeader h0(":path", "/");
  HPACKHeader h1(":path", "/");
  HPACKHeader h2(":path", "/index.php");
  HPACKHeader h3("x-fb-debug", "test");
  // ==
  EXPECT_TRUE(h0 == h1);
  EXPECT_FALSE(h1 == h2);
  // <
  EXPECT_FALSE(h1 < h1);
  EXPECT_TRUE(h1 < h2);
  EXPECT_TRUE(h1 < h3);
  // >
  EXPECT_FALSE(h2 > h2);
  EXPECT_TRUE(h3 > h2);
  EXPECT_TRUE(h2 > h1);

  stringstream out;
  out << h1;
  EXPECT_EQ(out.str(), ":path: /");
}

TEST_F(HPACKHeaderTests, has_value) {
  HPACKHeader h1(":path", "");
  HPACKHeader h2(":path", "/");
  EXPECT_FALSE(h1.hasValue());
  EXPECT_TRUE(h2.hasValue());
}

TEST_F(HPACKHeaderTests, is_indexable) {
  HPACKHeader path(":path", "index.php?q=42");
  EXPECT_FALSE(path.isIndexable());
  HPACKHeader cdn(":path", "/hprofile-ak-prn1/49496_6024432_1026115112_n.jpg");
  EXPECT_FALSE(cdn.isIndexable());
  HPACKHeader clen("content-length", "512");
  EXPECT_FALSE(clen.isIndexable());
}
