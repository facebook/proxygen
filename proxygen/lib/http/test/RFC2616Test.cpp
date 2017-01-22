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
#include <proxygen/lib/http/RFC2616.h>

using namespace proxygen;

using std::string;
using RFC2616::parseByteRangeSpec;

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

TEST(ByteRangeSpecTest, valids) {
  unsigned long firstByte = ULONG_MAX;
  unsigned long lastByte = ULONG_MAX;
  unsigned long instanceLength = ULONG_MAX;

  ASSERT_TRUE(
      parseByteRangeSpec(
        "bytes 0-10/100",
        firstByte, lastByte, instanceLength));
  EXPECT_EQ(0, firstByte);
  EXPECT_EQ(10, lastByte);
  EXPECT_EQ(100, instanceLength);

  ASSERT_TRUE(
      parseByteRangeSpec(
        "bytes */100",
        firstByte, lastByte, instanceLength));
  EXPECT_EQ(0, firstByte);
  EXPECT_EQ(ULONG_MAX, lastByte);
  EXPECT_EQ(100, instanceLength);

  ASSERT_TRUE(
      parseByteRangeSpec(
        "bytes 0-10/*",
        firstByte, lastByte, instanceLength));
  EXPECT_EQ(0, firstByte);
  EXPECT_EQ(10, lastByte);
  EXPECT_EQ(ULONG_MAX, instanceLength);
}

TEST(ByteRangeSpecTest, invalids) {
  unsigned long dummy;

  EXPECT_FALSE(parseByteRangeSpec("0-10/100", dummy, dummy, dummy)) <<
    "Spec must start with 'bytes '";
  EXPECT_FALSE(parseByteRangeSpec("bytes 10/100", dummy, dummy, dummy)) <<
    "Spec missing initial range";
  EXPECT_FALSE(parseByteRangeSpec("bytes 10-/100", dummy, dummy, dummy)) <<
    "Spec missing last byte in initial range";
  EXPECT_FALSE(parseByteRangeSpec("bytes 0-10 100", dummy, dummy, dummy)) <<
    "Spec missing '/' separator";
  EXPECT_FALSE(parseByteRangeSpec("bytes 0-10/100Q", dummy, dummy, dummy)) <<
    "Spec has trailing garbage";
  EXPECT_FALSE(parseByteRangeSpec("bytes 10-1/100", dummy, dummy, dummy)) <<
    "Spec initial range is invalid";
  EXPECT_FALSE(parseByteRangeSpec("bytes 10-90/50", dummy, dummy, dummy)) <<
    "Spec initial range is invalid too large";
  EXPECT_FALSE(parseByteRangeSpec("bytes x/100", dummy, dummy, dummy)) <<
    "Spec initial range has invalid first byte";
  EXPECT_FALSE(parseByteRangeSpec("bytes 0-x/100", dummy, dummy, dummy)) <<
    "Spec initial range has invalid last bytek";
  EXPECT_FALSE(parseByteRangeSpec("bytes *-10/100", dummy, dummy, dummy)) <<
    "Spec cannot contain wildcard in initial range";
  EXPECT_FALSE(parseByteRangeSpec("bytes 0-*/100", dummy, dummy, dummy)) <<
    "Spec cannot contain wildcard in initial range";

  folly::StringPiece sp("bytes 0-10/100");
  sp.subtract(3);
  EXPECT_FALSE(parseByteRangeSpec(sp, dummy, dummy, dummy)) <<
    "Spec StringPiece ends before instance length";
  sp.subtract(1);
  EXPECT_FALSE(parseByteRangeSpec(sp, dummy, dummy, dummy)) <<
    "Spec StringPiece ends before '/' character";
  sp.subtract(2);
  EXPECT_FALSE(parseByteRangeSpec(sp, dummy, dummy, dummy)) <<
    "Spec StringPiece ends before last byte in initial byte range";
  sp.subtract(1);
  EXPECT_FALSE(parseByteRangeSpec(sp, dummy, dummy, dummy)) <<
    "Spec StringPiece ends before '-' in initial byte range";
  sp.subtract(2);
  EXPECT_FALSE(parseByteRangeSpec(sp, dummy, dummy, dummy)) <<
    "Spec StringPiece ends before first byte in initial byte range";
}
