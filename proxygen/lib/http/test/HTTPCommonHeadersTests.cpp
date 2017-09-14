/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/HTTPCommonHeaders.h>


class HTTPCommonHeadersTests : public testing::Test {
};

TEST_F(HTTPCommonHeadersTests, test_hashing) {
  std::string common1("Content-Length");
  std::string common2("content-length");
  std::string uncommon("Uncommon");

  HTTPHeaderCode commonCode1 = HTTPCommonHeaders::hash(common1);
  HTTPHeaderCode commonCode2 = HTTPCommonHeaders::hash(common2);
  HTTPHeaderCode uncommonCode = HTTPCommonHeaders::hash(uncommon);

  CHECK_EQ(uncommonCode, HTTPHeaderCode::HTTP_HEADER_OTHER);
  CHECK_NE(commonCode1, HTTPHeaderCode::HTTP_HEADER_OTHER);

  CHECK_EQ(commonCode1, commonCode2);
}

TEST_F(HTTPCommonHeadersTests, test_two_tables_initialized) {
  std::string common("Content-Length");
  HTTPHeaderCode code = HTTPCommonHeaders::hash(common);

  CHECK_EQ(*HTTPCommonHeaders::getPointerToHeaderName(code), "Content-Length");
  CHECK_EQ(*HTTPCommonHeaders::getPointerToHeaderName(code, true),
           "content-length");
}
