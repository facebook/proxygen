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
#include <folly/Memory.h>
#include <proxygen/lib/utils/DestructorCheck.h>


using namespace proxygen;
using namespace testing;

class Derived : public DestructorCheck { };

TEST(DestructorCheckTest, WithoutGuard) {
  Derived d;
}

TEST(DestructorCheckTest, SingleGuard) {
  Derived d;
  Derived::Safety s(d);
  ASSERT_FALSE(s.destroyed());
}

TEST(DestructorCheckTest, SingleGuardDestroyed) {
  auto d = folly::make_unique<Derived>();
  Derived::Safety s(*d);
  ASSERT_FALSE(s.destroyed());
  d.reset();
  ASSERT_TRUE(s.destroyed());
}

TEST(DestructorCheckTest, MultipleGuards) {
  Derived d;
  auto s1 = folly::make_unique<Derived::Safety>(d);
  auto s2 = folly::make_unique<Derived::Safety>(d);
  auto s3 = folly::make_unique<Derived::Safety>(d);

  // Remove the middle of the list.
  ASSERT_FALSE(s2->destroyed());
  s2.reset();

  // Add in a link after a removal has occurred.
  auto s4 = folly::make_unique<Derived::Safety>(d);

  // Remove the beginning of the list.
  ASSERT_FALSE(s1->destroyed());
  s1.reset();
  // Remove the end of the list.
  ASSERT_FALSE(s4->destroyed());
  s4.reset();
  // Remove the last remaining of the list.
  ASSERT_FALSE(s3->destroyed());
  s3.reset();
}

TEST(DestructorCheckTest, MultipleGuardsDestroyed) {
  auto d = folly::make_unique<Derived>();
  auto s1 = folly::make_unique<Derived::Safety>(*d);
  auto s2 = folly::make_unique<Derived::Safety>(*d);
  auto s3 = folly::make_unique<Derived::Safety>(*d);
  auto s4 = folly::make_unique<Derived::Safety>(*d);

  // Remove something from the list.
  ASSERT_FALSE(s2->destroyed());
  s2.reset();

  ASSERT_FALSE(s1->destroyed());
  ASSERT_FALSE(s3->destroyed());
  ASSERT_FALSE(s4->destroyed());

  d.reset();

  ASSERT_TRUE(s1->destroyed());
  ASSERT_TRUE(s3->destroyed());
  ASSERT_TRUE(s4->destroyed());
}
