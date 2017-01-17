/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <glog/logging.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/codec/compress/HeaderPiece.h>

using namespace proxygen::compress;
using namespace testing;

class HeaderPieceTests : public testing::Test {};

TEST_F(HeaderPieceTests, basic) {
  HeaderPiece *hp;

  // creating non-owner piece with null pointer
  hp = new HeaderPiece(nullptr, 0, false, true);
  EXPECT_TRUE(hp->isMultiValued());
  // destructing this should be fine, since will not try to release the memory
  delete hp;

  char *buf = new char[16];
  hp = new HeaderPiece(buf, 16, true, true);
  EXPECT_EQ(hp->str.data(), buf);
  EXPECT_EQ(hp->str.size(), 16);
  // this should release the mem
  delete hp;
}
