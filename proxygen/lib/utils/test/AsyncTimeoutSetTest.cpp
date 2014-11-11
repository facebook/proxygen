/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/async/EventBase.h>
#include <folly/io/async/test/UndelayedDestruction.h>
#include <folly/io/async/test/Util.h>
#include <gtest/gtest.h>
#include <proxygen/lib/utils/AsyncTimeoutSet.h>
#include <vector>

using namespace proxygen;

using std::chrono::milliseconds;

typedef folly::UndelayedDestruction<AsyncTimeoutSet> StackTimeoutSet;

class TestTimeout : public AsyncTimeoutSet::Callback {
 public:
  template<typename ...Args>
  explicit TestTimeout(Args&& ...args) {
    addTimeout(std::forward<Args>(args)...);
    _scheduleNext();
  }
  TestTimeout() {}

  void addTimeout(AsyncTimeoutSet* set) {
    nextSets_.push_back(set);
  }

  template<typename ...Args>
  void addTimeout(AsyncTimeoutSet* set, Args&& ...args) {
    addTimeout(set);
    addTimeout(std::forward<Args>(args)...);
  }

  virtual void timeoutExpired() noexcept {
    timestamps.emplace_back();
    _scheduleNext();
    if (fn) {
      fn();
    }
  }

  void _scheduleNext() {
    if (nextSets_.empty()) {
      return;
    }
    AsyncTimeoutSet* nextSet = nextSets_.front();
    nextSets_.pop_front();
    nextSet->scheduleTimeout(this);
  }

  std::deque<folly::TimePoint> timestamps;
  std::function<void()> fn;

 private:
  std::deque<AsyncTimeoutSet*> nextSets_;
};

/*
 * Test firing some simple timeouts that are fired once and never rescheduled
 */
TEST(TimeoutSet, FireOnce) {
  folly::EventBase eventBase;
  StackTimeoutSet ts10(&eventBase, milliseconds(10));
  StackTimeoutSet ts5(&eventBase, milliseconds(5));

  const AsyncTimeoutSet::Callback* nullCallback = nullptr;
  ASSERT_EQ(ts10.front(), nullCallback);
  ASSERT_EQ(ts5.front(), nullCallback);

  TestTimeout t1;
  TestTimeout t2;
  TestTimeout t3;

  ts5.scheduleTimeout(&t1);
  ts5.scheduleTimeout(&t2);
  ts10.scheduleTimeout(&t3);

  ASSERT_EQ(ts10.front(), &t3);
  ASSERT_EQ(ts5.front(), &t1);

  folly::TimePoint start;
  eventBase.loop();
  folly::TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
  ASSERT_EQ(t3.timestamps.size(), 1);

  ASSERT_EQ(ts10.front(), nullCallback);
  ASSERT_EQ(ts5.front(), nullCallback);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(start, t2.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(start, t3.timestamps[0], milliseconds(10));
  T_CHECK_TIMEOUT(start, end, milliseconds(10));
}

/*
 * Test some timeouts that are scheduled on one timeout set, then moved to
 * another timeout set.
 */
TEST(TimeoutSet, SwitchTimeoutSet) {
  folly::EventBase eventBase;
  StackTimeoutSet ts10(&eventBase, milliseconds(10));
  StackTimeoutSet ts5(&eventBase, milliseconds(5));

  TestTimeout t1(&ts5, &ts10, &ts5);
  TestTimeout t2(&ts10, &ts10, &ts5);
  TestTimeout t3(&ts5, &ts5, &ts10, &ts5);

  ts5.scheduleTimeout(&t1);

  folly::TimePoint start;
  eventBase.loop();
  folly::TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 3);
  ASSERT_EQ(t2.timestamps.size(), 3);
  ASSERT_EQ(t3.timestamps.size(), 4);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(t1.timestamps[0], t1.timestamps[1], milliseconds(10));
  T_CHECK_TIMEOUT(t1.timestamps[1], t1.timestamps[2], milliseconds(5));

  T_CHECK_TIMEOUT(start, t2.timestamps[0], milliseconds(10));
  T_CHECK_TIMEOUT(t2.timestamps[0], t2.timestamps[1], milliseconds(10));
  T_CHECK_TIMEOUT(t2.timestamps[1], t2.timestamps[2], milliseconds(5));

  T_CHECK_TIMEOUT(start, t3.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(t3.timestamps[0], t3.timestamps[1], milliseconds(5));
  T_CHECK_TIMEOUT(t3.timestamps[1], t3.timestamps[2], milliseconds(10));
  T_CHECK_TIMEOUT(t3.timestamps[2], t3.timestamps[3], milliseconds(5));

  // 10ms fudge factor to account for loaded machines
  T_CHECK_TIMEOUT(start, end, milliseconds(25), milliseconds(10));
}

/*
 * Test cancelling a timeout when it is scheduled to be fired right away.
 */
TEST(TimeoutSet, CancelTimeout) {
  folly::EventBase eventBase;
  StackTimeoutSet ts5(&eventBase, milliseconds(5));
  StackTimeoutSet ts10(&eventBase, milliseconds(10));
  StackTimeoutSet ts20(&eventBase, milliseconds(20));

  // Create several timeouts that will all fire in 5ms.
  TestTimeout t5_1(&ts5);
  TestTimeout t5_2(&ts5);
  TestTimeout t5_3(&ts5);
  TestTimeout t5_4(&ts5);
  TestTimeout t5_5(&ts5);

  // Also create a few timeouts to fire in 10ms
  TestTimeout t10_1(&ts10);
  TestTimeout t10_2(&ts10);
  TestTimeout t10_3(&ts10);

  TestTimeout t20_1(&ts20);
  TestTimeout t20_2(&ts20);

  // Have t5_1 cancel t5_2 and t5_4.
  //
  // Cancelling t5_2 will test cancelling a timeout that is at the head of the
  // list and ready to be fired.
  //
  // Cancelling t5_4 will test cancelling a timeout in the middle of the list
  t5_1.fn = [&] {
    t5_2.cancelTimeout();
    t5_4.cancelTimeout();
  };

  // Have t5_3 cancel t5_5.
  // This will test cancelling the last remaining timeout.
  //
  // Then have t5_3 reschedule itself.
  t5_3.fn = [&] {
    t5_5.cancelTimeout();
    // Reset our function so we won't continually reschedule ourself
    auto fn = std::move(t5_3.fn);
    ts5.scheduleTimeout(&t5_3);

    // Also test cancelling timeouts in another timeset that isn't ready to
    // fire yet.
    //
    // Cancel the middle timeout in ts10.
    t10_2.cancelTimeout();
    // Cancel both the timeouts in ts20.
    t20_1.cancelTimeout();
    t20_2.cancelTimeout();
  };

  folly::TimePoint start;
  eventBase.loop();
  folly::TimePoint end;

  ASSERT_EQ(t5_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_1.timestamps[0], milliseconds(5));

  ASSERT_EQ(t5_3.timestamps.size(), 2);
  T_CHECK_TIMEOUT(start, t5_3.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(t5_3.timestamps[0], t5_3.timestamps[1], milliseconds(5));

  ASSERT_EQ(t10_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t10_1.timestamps[0], milliseconds(10));
  ASSERT_EQ(t10_3.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t10_3.timestamps[0], milliseconds(10));

  // Cancelled timeouts
  ASSERT_EQ(t5_2.timestamps.size(), 0);
  ASSERT_EQ(t5_4.timestamps.size(), 0);
  ASSERT_EQ(t5_5.timestamps.size(), 0);
  ASSERT_EQ(t10_2.timestamps.size(), 0);
  ASSERT_EQ(t20_1.timestamps.size(), 0);
  ASSERT_EQ(t20_2.timestamps.size(), 0);

  T_CHECK_TIMEOUT(start, end, milliseconds(10));
}

/*
 * Test destroying a AsyncTimeoutSet with timeouts outstanding
 */
TEST(TimeoutSet, DestroyTimeoutSet) {
  folly::EventBase eventBase;

  AsyncTimeoutSet::UniquePtr ts5(new AsyncTimeoutSet(
        &eventBase, milliseconds(5)));
  AsyncTimeoutSet::UniquePtr ts10(new AsyncTimeoutSet(
        &eventBase, milliseconds(10)));

  TestTimeout t5_1(ts5.get());
  TestTimeout t5_2(ts5.get());
  TestTimeout t5_3(ts5.get());

  TestTimeout t10_1(ts10.get());
  TestTimeout t10_2(ts10.get());

  // Have t5_1 destroy ts10
  t5_1.fn = [&] { ts10.reset(); };
  // Have t5_2 destroy ts5
  // Note that this will call destroy() on ts5 inside ts5's timeoutExpired()
  // method.
  t5_2.fn = [&] { ts5.reset(); };

  folly::TimePoint start;
  eventBase.loop();
  folly::TimePoint end;

  ASSERT_EQ(t5_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_1.timestamps[0], milliseconds(5));
  ASSERT_EQ(t5_2.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_2.timestamps[0], milliseconds(5));

  ASSERT_EQ(t5_3.timestamps.size(), 0);
  ASSERT_EQ(t10_1.timestamps.size(), 0);
  ASSERT_EQ(t10_2.timestamps.size(), 0);

  T_CHECK_TIMEOUT(start, end, milliseconds(5));
}

/*
 * Test the atMostEveryN parameter, to ensure that the timeout does not fire
 * too frequently.
 */
TEST(TimeoutSet, AtMostEveryN) {
  folly::EventBase eventBase;

  // Create a timeout set with a 10ms interval, to fire no more than once
  // every 3ms.
  milliseconds interval(25);
  milliseconds atMostEveryN(6);
  StackTimeoutSet ts10(&eventBase, interval, atMostEveryN);

  // Create 60 timeouts to be added to ts10 at 1ms intervals.
  uint32_t numTimeouts = 60;
  std::vector<TestTimeout> timeouts(numTimeouts);

  // Create a scheduler timeout to add the timeouts 1ms apart.
  uint32_t index = 0;
  StackTimeoutSet ts1(&eventBase, milliseconds(1));
  TestTimeout scheduler(&ts1);
  scheduler.fn = [&] {
    if (index >= numTimeouts) {
      return;
    }
    // Call timeoutExpired() on the timeout so it will record a timestamp.
    // This is done only so we can record when we scheduled the timeout.
    // This way if ts1 starts to fall behind a little over time we will still
    // be comparing the ts10 timeouts to when they were first scheduled (rather
    // than when we intended to schedule them).  The scheduler may fall behind
    // eventually since we don't really schedule it once every millisecond.
    // Each time it finishes we schedule it for 1 millisecond in the future.
    // The amount of time it takes to run, and any delays it encounters
    // getting scheduled may eventually add up over time.
    timeouts[index].timeoutExpired();

    // Schedule the new timeout
    ts10.scheduleTimeout(&timeouts[index]);
    // Reschedule ourself
    ts1.scheduleTimeout(&scheduler);
    ++index;
  };

  // Go ahead and schedule the first timeout now.
  scheduler.fn();

  eventBase.loop();

  // We scheduled timeouts 1ms apart, when the AsyncTimeoutSet is only allowed
  // to wake up at most once every 3ms.  It will therefore wake up every 3ms
  // and fire groups of approximately 3 timeouts at a time.
  //
  // This is "approximately 3" since it may get slightly behind and fire 4 in
  // one interval, etc.  CHECK_TIMEOUT normally allows a few milliseconds of
  // tolerance.  We have to add the same into our checking algorithm here.
  for (uint32_t idx = 0; idx < numTimeouts; ++idx) {
    ASSERT_EQ(timeouts[idx].timestamps.size(), 2);

    folly::TimePoint scheduledTime(timeouts[idx].timestamps[0]);
    folly::TimePoint firedTime(timeouts[idx].timestamps[1]);

    // Assert that the timeout fired at roughly the right time.
    // CHECK_TIMEOUT() normally has a tolerance of 5ms.  Allow an additional
    // atMostEveryN.
    milliseconds tolerance = milliseconds(5) + atMostEveryN;
    T_CHECK_TIMEOUT(scheduledTime, firedTime, interval, tolerance);

    // Assert that the difference between the previous timeout and now was
    // either very small (fired in the same event loop), or larger than
    // atMostEveryN.
    if (idx == 0) {
      // no previous value
      continue;
    }
    folly::TimePoint prev(timeouts[idx - 1].timestamps[1]);

    auto delta = (firedTime.getTimeStart() - prev.getTimeEnd()) -
      (firedTime.getTimeWaiting() - prev.getTimeWaiting());
    if (delta > milliseconds(1)) {
      T_CHECK_TIMEOUT(prev, firedTime, atMostEveryN);
    }
  }
}
