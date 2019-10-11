/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/portability/GTest.h>
#include <proxygen/lib/utils/UtilInl.h>

using namespace proxygen;

TEST(UtilTest, CaseInsensitiveEqual) {
  ASSERT_TRUE(caseInsensitiveEqual("foo", "FOO"));
  ASSERT_TRUE(caseInsensitiveEqual(std::string("foo"), "FOO"));
  ASSERT_FALSE(caseInsensitiveEqual(std::string("foo"), "FOO2"));
  ASSERT_FALSE(caseInsensitiveEqual("fo", "FOO"));
  ASSERT_FALSE(caseInsensitiveEqual("FO", "FOO"));
}
