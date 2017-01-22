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
#include <memory>
#include <proxygen/lib/utils/Result.h>

using namespace proxygen;
using namespace std;

static_assert(sizeof(Result<char, uint8_t>) == 2, "");

TEST(Result, Copy) {
  // This instance of Result is copyable
  Result<char, int> expected = 'b';
  Result<char, int> error = 42;

  EXPECT_TRUE(expected.isOk());
  EXPECT_FALSE(expected.isError());
  EXPECT_FALSE(error.isOk());
  EXPECT_TRUE(error.isError());

  EXPECT_EQ('b', expected.ok());
  EXPECT_EQ(42, error.error());

  auto expectedCopy = expected;
  auto errorCopy = error;

  EXPECT_TRUE(expectedCopy.isOk());
  EXPECT_FALSE(expectedCopy.isError());
  EXPECT_FALSE(errorCopy.isOk());
  EXPECT_TRUE(errorCopy.isError());

  EXPECT_EQ(expectedCopy.ok(), expected.ok());
  EXPECT_EQ(errorCopy.error(), error.error());
}

TEST(Result, Move) {
  struct ErrStruct {
    ErrStruct(int foo): val(foo) {}
    ErrStruct(const ErrStruct& src): val(src.val) {}
    ErrStruct(ErrStruct&& src) noexcept: val(src.val) {
      src.moved = true;
    }
    int val;
    bool moved{false};
  };
  static_assert(std::is_nothrow_move_constructible<ErrStruct>::value,
                "This struct must be nothrow move constructible");

  Result<string, ErrStruct> original{4};

  // move
  auto error = std::move(original);

  EXPECT_TRUE(original.error().moved);
  EXPECT_TRUE(!error.error().moved);
}

TEST(Result, Assign) {
  bool intWasDeleted = false;
  auto deleter = [&](int* ptr) {
    intWasDeleted = true;
    delete ptr;
  };
  typedef unique_ptr<int, decltype(deleter)> int_ptr;

  // emplace
  Result<char, int_ptr> result = 'a';

  // copy assign
  EXPECT_EQ('a', result.ok());
  result = 'b';
  EXPECT_EQ('b', result.ok());

  // move assign
  result = int_ptr(new int(5), deleter);
  EXPECT_EQ(5, *result.error());

  // check deletion
  EXPECT_FALSE(intWasDeleted);
  result = 'c';
  EXPECT_TRUE(intWasDeleted);
  EXPECT_EQ('c', result.ok());
}

TEST(Result, Emplace) {
  int constructCount = 0;
  struct Expensive {
    /* implicit */ Expensive(int* counter) {
      ++*counter;
    }
    Expensive(const Expensive& other) {
      EXPECT_TRUE(false); // Should not be invoked
    }
    Expensive(Expensive&& other) {
      EXPECT_TRUE(false); // Should not be invoked
    }
  };

  // Use emplacement so we only construct the Expensive object once.
  auto tester = make_ok<Expensive, char>(&constructCount);
  EXPECT_EQ(1, constructCount);
  EXPECT_TRUE(tester.isOk());
}
