/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <assert.h>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace proxygen {

/**
 * The Result type. It is similar to Haskell's Either type and Rust's
 * Result type. It is essentially a tagged union. The type T represents
 * the expected type and E represents the error type.
 *
 * This implementation is also inspired by the C++17 proposal n4109:
 * http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4109.pdf
 *
 * The interface is patterned after
 * http://doc.rust-lang.org/std/result/enum.Result.html
 *
 * In the future, we can add an interface that takes two callbacks and
 * executes the appropriate one based on whether the Result is an error or
 * if it is ok.
 *
 * We also provide two factory functions to construct Results using
 * emplacement. Ideally we would support this in the constructor of Result
 * since we already forbid the ok type to equal the error type.
 */
template <typename T, typename E>
class Result {
 public:
  static_assert(!std::is_same<T, E>::value,
                "The result and error types must be different");

  // Emplace constructor TODO XXX
  // We need to prefer the variadic template form over the non-template
  // implicit conversion constructors for this to work correctly.
  // template <typename... Args,
  //           typename = std::enable_if_t<
  //             std::is_constructible<T, Args...>::value ||
  //             std::is_constructible<E, Args...>::value
  //             > >
  //   Result(Args&&... args) {
  // }

  Result(Result&& src) noexcept(
    std::is_nothrow_move_constructible<T>::value &&
    std::is_nothrow_move_constructible<E>::value):
  state_(src.state_) {

    if (src.isOk()) {
      constructOk(std::move(src.ok()));
    } else {
      constructError(std::move(src.error()));
    }
  }

  Result(const Result& src) noexcept(
    std::is_nothrow_copy_constructible<T>::value &&
    std::is_nothrow_copy_constructible<E>::value):
  state_(src.state_) {

    if (src.isOk()) {
      constructOk(src.ok());
    } else {
      constructError(src.error());
    }
  }

  /* implicit */ Result(T&& okVal) noexcept(
    std::is_nothrow_move_constructible<T>::value):
  state_(State::IS_OK) {
    constructOk(std::move(okVal));
  }

  /* implicit */ Result(const T& okVal) noexcept(
    std::is_nothrow_copy_constructible<T>::value):
  state_(State::IS_OK) {
    constructOk(okVal);
  }

  /* implicit */ Result(E&& errorVal) noexcept(
    std::is_nothrow_move_constructible<E>::value):
  state_(State::IS_ERROR) {
    constructError(std::move(errorVal));
  }

  /* implicit */ Result(const E& errorVal) noexcept(
    std::is_nothrow_copy_constructible<E>::value):
  state_(State::IS_ERROR) {
    constructError(errorVal);
  }

  ~Result() noexcept {
    clear();
  }

  bool isOk() const noexcept {
    return state_ == State::IS_OK;
  }

  bool isError() const noexcept {
    return state_ == State::IS_ERROR;
  }

  T& ok() {
    assert(isOk());
    return storage_.ok;
  }

  const T& ok() const {
    assert(isOk());
    return storage_.ok;
  }

  E& error() {
    assert(isError());
    return storage_.error;
  }

  const E& error() const {
    assert(isError());
    return storage_.error;
  }

  Result& operator=(T&& arg) {
    clear();
    constructOk(std::forward<T>(arg));
    return *this;
  }

  Result& operator=(E&& arg) {
    clear();
    constructError(std::forward<E>(arg));
    return *this;
  }

  Result& operator=(Result&& src)
    noexcept (std::is_nothrow_move_constructible<T>::value &&
              std::is_nothrow_move_constructible<E>::value) {

    clear();
    if (src.isOk()) {
      constructOk(std::move(src.ok()));
    } else {
      constructError(std::move(src.error()));
    }
    return *this;
  }

  Result& operator=(const Result& src)
    noexcept (std::is_nothrow_copy_constructible<T>::value &&
              std::is_nothrow_copy_constructible<E>::value) {

    clear();
    if (src.isOk()) {
      constructOk(src.ok());
    } else {
      constructError(src.error());
    }
    return *this;
  }

  template <typename T_, typename E_, typename... Args>
  friend Result<T_, E_> make_ok(Args&&...);

  template <typename T_, typename E_, typename... Args>
  friend Result<T_, E_> make_error(Args&&...);

 private:

  // For use by friend factory functions only
  Result() noexcept {
  }

  void clear() noexcept {
    if (isOk()) {
      storage_.ok.~T();
    } else {
      storage_.error.~E();
    }
  }

  template<class... Args>
  void constructOk(Args&&... args) {
    new(&storage_) T(std::forward<Args>(args)...);
    state_ = State::IS_OK;
  }

  template<class... Args>
  void constructError(Args&&... args) {
    new(&storage_) E(std::forward<Args>(args)...);
    state_ = State::IS_ERROR;
  }

  union Storage {
    Storage() {}
    ~Storage() {}
    T ok;
    E error;
  };
  Storage storage_;

  enum class State: uint8_t {
    IS_OK,
    IS_ERROR,
  };
  State state_;
};

/**
 * Efficiently constructs a Result<T, E> while avoiding any implicit
 * conversions. This function is preferred when emplacement is required.
 */
template <typename T, typename E, typename... Args>
Result<T, E> make_ok(Args&&... args) {
  Result<T, E> result;  // temporarily empty
  result.constructOk(std::forward<Args>(args)...);
  return result;
}

/**
 * Efficiently constructs a Result<T, E> while avoiding any implicit
 * conversions. This function is preferred when emplacement is required.
 */
template <typename T, typename E, typename... Args>
Result<T, E> make_error(Args&&... args) {
  Result<T, E> result;  // temporarily empty
  result.constructError(std::forward<Args>(args)...);
  return result;
}

}
