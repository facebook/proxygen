/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>
#include <proxygen/lib/utils/Base64.h>

using namespace testing;
using namespace proxygen;
using std::string;

namespace {
folly::ByteRange range(const char *str) {
  return folly::ByteRange((const unsigned char *)str, strlen(str));
}
}


TEST(Base64, Encode) {
  EXPECT_EQ(Base64::encode(range("hello world")), "aGVsbG8gd29ybGQ=");
}

TEST(Base64, EncodeDecodeNL) {
  EXPECT_EQ(Base64::encode(range("hello \nworld")), "aGVsbG8gCndvcmxk");
  EXPECT_EQ(Base64::decode("aGVsbG8gCndvcmxk"), "hello \nworld");
}

TEST(Base64, Decode) {
  EXPECT_EQ(Base64::decode("aGVsbG8gd29ybGQ="), "hello world");
}

TEST(Base64, DecodeEmpty) {
  EXPECT_EQ(Base64::decode(""), "");
}

TEST(Base64, DecodeGarbage) {
  EXPECT_EQ(Base64::decode("[[[[["), "");
}

TEST(Base64, DecodePadding) {
  EXPECT_EQ(Base64::decode("YQ=="), "a");
}
