/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>
#include <proxygen/lib/http/RFC2616.h>

using namespace proxygen;

using std::string;

TEST(QvalueTest, basic) {

  std::vector<RFC2616::TokenQPair> output;

  string test1("iso-8859-5, unicode-1-1;q=0.8");
  EXPECT_TRUE(RFC2616::parseQvalues(test1, output));
  EXPECT_EQ(output.size(), 2);
  EXPECT_EQ(output[0].first.compare(folly::StringPiece("iso-8859-5")), 0);
  EXPECT_DOUBLE_EQ(output[0].second, 1);
  EXPECT_EQ(output[1].first.compare(folly::StringPiece("unicode-1-1")), 0);
  EXPECT_DOUBLE_EQ(output[1].second, 0.8);
  output.clear();

  string test2("compress, gzip");
  EXPECT_TRUE(RFC2616::parseQvalues(test2, output));
  EXPECT_EQ(output.size(), 2);
  EXPECT_EQ(output[0].first.compare(folly::StringPiece("compress")), 0);
  EXPECT_DOUBLE_EQ(output[0].second, 1);
  EXPECT_EQ(output[1].first.compare(folly::StringPiece("gzip")), 0);
  EXPECT_DOUBLE_EQ(output[1].second, 1);
  output.clear();

  string test3("");
  // The spec says a blank one is ok but empty headers are disallowed in SPDY?
  EXPECT_FALSE(RFC2616::parseQvalues(test3, output));
  EXPECT_EQ(output.size(), 0);

  string test4("compress;q=0.5, gzip;q=1.0");
  EXPECT_TRUE(RFC2616::parseQvalues(test4, output));
  EXPECT_EQ(output.size(), 2);
  EXPECT_EQ(output[0].first.compare(folly::StringPiece("compress")), 0);
  EXPECT_DOUBLE_EQ(output[0].second, 0.5);
  EXPECT_EQ(output[1].first.compare(folly::StringPiece("gzip")), 0);
  EXPECT_DOUBLE_EQ(output[1].second, 1.0);
  output.clear();

  string test5("gzip;q=1.0, identity; q=0.5, *;q=0");
  EXPECT_TRUE(RFC2616::parseQvalues(test5, output));
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].first.compare(folly::StringPiece("gzip")), 0);
  EXPECT_DOUBLE_EQ(output[0].second, 1);
  EXPECT_EQ(output[1].first.compare(folly::StringPiece("identity")), 0);
  EXPECT_DOUBLE_EQ(output[1].second, 0.5);
  EXPECT_EQ(output[2].first.compare(folly::StringPiece("*")), 0);
  EXPECT_DOUBLE_EQ(output[2].second, 0);
  output.clear();

  string test6("da, en-gb;q=0.8, en;q=0.7");
  EXPECT_TRUE(RFC2616::parseQvalues(test6, output));
  EXPECT_EQ(output.size(), 3);
  EXPECT_EQ(output[0].first.compare(folly::StringPiece("da")), 0);
  EXPECT_DOUBLE_EQ(output[0].second, 1);
  EXPECT_EQ(output[1].first.compare(folly::StringPiece("en-gb")), 0);
  EXPECT_DOUBLE_EQ(output[1].second, 0.8);
  EXPECT_EQ(output[2].first.compare(folly::StringPiece("en")), 0);
  EXPECT_DOUBLE_EQ(output[2].second, 0.7);
  output.clear();
}

TEST(QvalueTest, extras) {

  std::vector<RFC2616::TokenQPair> output;

  string test1("  iso-8859-5,    unicode-1-1;  q=0.8  hi mom!");
  EXPECT_TRUE(RFC2616::parseQvalues(test1, output));
  EXPECT_EQ(output.size(), 2);
  EXPECT_EQ(output[0].first.compare(folly::StringPiece("iso-8859-5")), 0);
  EXPECT_DOUBLE_EQ(output[0].second, 1);
  EXPECT_EQ(output[1].first.compare(folly::StringPiece("unicode-1-1")), 0);
  EXPECT_DOUBLE_EQ(output[1].second, 0.8);
  output.clear();

  string test2("gzip");
  EXPECT_TRUE(RFC2616::parseQvalues(test2, output));
  EXPECT_EQ(output.size(), 1);
  EXPECT_EQ(output[0].first.compare(folly::StringPiece("gzip")), 0);
  EXPECT_DOUBLE_EQ(output[0].second, 1);
  output.clear();
}

TEST(QvalueTest, invalids) {

  std::vector<RFC2616::TokenQPair> output;

  string test1(",,,");
  EXPECT_FALSE(RFC2616::parseQvalues(test1, output));
  EXPECT_EQ(output.size(), 0);
  output.clear();

  string test2("  ; q=0.1");
  EXPECT_FALSE(RFC2616::parseQvalues(test2, output));
  EXPECT_EQ(output.size(), 0);
  output.clear();

  string test3("gzip; q=uietplease");
  EXPECT_FALSE(RFC2616::parseQvalues(test3, output));
  EXPECT_EQ(output.size(), 1);
  EXPECT_EQ(output[0].first.compare(folly::StringPiece("gzip")), 0);
  EXPECT_DOUBLE_EQ(output[0].second, 1);
  output.clear();

  string test4("gzip; whoohoo, defalte");
  EXPECT_FALSE(RFC2616::parseQvalues(test4, output));
  EXPECT_EQ(output.size(), 2);
  EXPECT_EQ(output[0].first.compare(folly::StringPiece("gzip")), 0);
  EXPECT_DOUBLE_EQ(output[0].second, 1);
  EXPECT_EQ(output[1].first.compare(folly::StringPiece("defalte")), 0);
  EXPECT_DOUBLE_EQ(output[1].second, 1);
  output.clear();

}
