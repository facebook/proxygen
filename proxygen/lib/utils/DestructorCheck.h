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
    rootGuard_.setAllDestroyed();
  }

  class Safety;

  class ForwardLink {
   // These methods are mostly private because an outside caller could violate
   // the integrity of the linked list.
   private:
    void setAllDestroyed() {
      for (auto guard = next_; guard; guard = guard->next_) {
        guard->setDestroyed();
      }
    }

    // This is used to maintain the double-linked list. An intrusive list does
    // not require any heap allocations, like a standard container would. This
    // isolation of next_ in its own class means that the DestructorCheck can
    // easily hold a next_ pointer without needing to hold a prev_ pointer.
    // DestructorCheck never needs a prev_ pointer because it is the head node
    // and this is a special list where the head never moves and never has a
    // previous node.
    Safety* next_{nullptr};

    friend DestructorCheck::~DestructorCheck();
    friend class Safety;
  };

  // See above example for usage
  class Safety : public ForwardLink {
   public:
    explicit Safety(DestructorCheck& destructorCheck) {
      // Insert this node at the head of the list.
      prev_ = &destructorCheck.rootGuard_;
      next_ = prev_->next_;
      if (next_ != nullptr) {
        next_->prev_ = this;
      }
      prev_->next_ = this;
     }

    ~Safety() {
      if (!destroyed()) {
        // Remove this node from the list.
        prev_->next_ = next_;
        if (next_ != nullptr) {
          next_->prev_ = prev_;
        }
      }
    }

    bool destroyed() const {
      return prev_ == nullptr;
    }

   private:
    void setDestroyed() {
      prev_ = nullptr;
    }

    // This field is used to maintain the double-linked list. If the root has
    // been destroyed then the field is set to the nullptr sentinel value.
    ForwardLink* prev_;

    friend class ForwardLink;
  };

 private:
  ForwardLink rootGuard_;
};

}
