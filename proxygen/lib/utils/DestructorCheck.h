/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

namespace proxygen {

/**
 * DestructorCheck marks a boolean when it is destroyed.  You can use it with
 * the inner Safety class to know if the DestructorCheck was destroyed during
 * some asynchronous call.
 *
 *
 * Example:
 *
 * class AsyncFoo : public DestructorCheck {
 *  public:
 *   ~AsyncFoo();
 *   // awesome async code with circuitous deletion paths
 *   void async1();
 *   void async2();
 * };
 *
 * righteousFunc(AsyncFoo& f) {
 *   DestructorCheck::Safety safety(f);
 *
 *   f.async1(); // might have deleted f, oh noes
 *   if (!safety.destroyed()) {
 *     // phew, still there
 *     f.async2();
 *   }
 * }
 */

class DestructorCheck {
 public:
  virtual ~DestructorCheck() {
    if (pDestroyed_) {
      *pDestroyed_ = true;
    }
  }

  void setDestroyedPtr(bool* pDestroyed) {
    pDestroyed_ = pDestroyed;
  }

  void clearDestroyedPtr() {
    pDestroyed_ = nullptr;
  }

  // See above example for usage
  class Safety {
   public:
    explicit Safety(DestructorCheck& destructorCheck)
      : destructorCheck_(destructorCheck) {
      destructorCheck_.setDestroyedPtr(&destroyed_);
     }

    ~Safety() {
      if (!destroyed_) {
        destructorCheck_.clearDestroyedPtr();
      }
    }

    bool destroyed() const {
      return destroyed_;
    }

   private:
    DestructorCheck& destructorCheck_;
    bool destroyed_{false};
  };

 private:
  bool* pDestroyed_{nullptr};
};

}
