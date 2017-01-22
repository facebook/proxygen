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
#include <proxygen/lib/utils/Base64.h>

using namespace testing;
using namespace proxygen;
using std::string;

namespace {
folly::ByteRange range(const char *str, int64_t len = -1) {
  return folly::ByteRange((const unsigned char *)str,
                          len >= 0 ? len : strlen(str));
}
}


TEST(Base64, Encode) {
  EXPECT_EQ(Base64::urlEncode(range("hello world")), "aGVsbG8gd29ybGQ");
}

TEST(Base64, EncodeDecodeNL) {
  EXPECT_EQ(Base64::urlEncode(range("hello \nworld")), "aGVsbG8gCndvcmxk");
  EXPECT_EQ(Base64::urlDecode("aGVsbG8gCndvcmxk"), "hello \nworld");
}

TEST(Base64, Decode) {
  EXPECT_EQ(Base64::urlDecode("aGVsbG8gd29ybGQ"), "hello world");
}

TEST(Base64, DecodeEmpty) {
  EXPECT_EQ(Base64::urlDecode(""), "");
}

TEST(Base64, DecodeGarbage) {
  EXPECT_EQ(Base64::urlDecode("[[[[["), "");
}

TEST(Base64, DecodePadding) {
  EXPECT_EQ(Base64::urlDecode("YQ"), "a");
  EXPECT_EQ(Base64::urlDecode("YWE"), "aa");
  EXPECT_EQ(Base64::urlDecode("YWFh"), "aaa");
}

TEST(Base64, EncodeDecodeHighBits) {
  EXPECT_EQ(Base64::urlDecode("_--_"), std::string("\xff\xef\xbf", 3));
  EXPECT_EQ(Base64::urlEncode(range("\xff\xef\xbf", 3)), "_--_");
}
