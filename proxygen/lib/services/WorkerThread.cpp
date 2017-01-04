/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/services/WorkerThread.h>

#include <folly/Portability.h>
#include <folly/String.h>
#include <folly/io/async/EventBaseManager.h>
#include <glog/logging.h>
#include <signal.h>

namespace proxygen {

FOLLY_TLS WorkerThread* WorkerThread::currentWorker_ = nullptr;

std::atomic_uint WorkerThread::objectCounter_;

WorkerThread::WorkerThread(folly::EventBaseManager* eventBaseManager)
    : eventBaseManager_(eventBaseManager) {
  //eventBase_.setName(folly::to<std::string>("WorkerThread",
  //                                          objectCounter_.fetch_add(1)));
}

WorkerThread::~WorkerThread() {
  CHECK(state_ == State::IDLE);
}

void WorkerThread::start() {
  CHECK(state_ == State::IDLE);
  state_ = State::STARTING;

  {
    // because you could theoretically call wait in parallel with start,
    // why are you in such a hurry anyways?
    std::lock_guard<std::mutex> guard(joinLock_);
    thread_ = std::thread([&]() mutable {
        this->setup();
        this->runLoop();
        this->cleanup();
      });
  }
  eventBase_.waitUntilRunning();
  // The server has been set up and is now in the loop implementation
}

void WorkerThread::stopWhenIdle() {
  // Call runInEventBaseThread() to perform all of the work in the actual
  // worker thread.
  //
  // This way we don't have to synchronize access to state_.
  eventBase_.runInEventBaseThread([this] {
    if (state_ == State::RUNNING) {
      state_ = State::STOP_WHEN_IDLE;
      eventBase_.terminateLoopSoon();
    // state_ could be IDLE if we don't execute this callback until the
    // EventBase is destroyed in the WorkerThread destructor
    } else if (state_ != State::IDLE && state_ != State::STOP_WHEN_IDLE) {
      LOG(FATAL) << "stopWhenIdle() called in unexpected state " <<
          static_cast<int>(state_);
    }
  });
}

void WorkerThread::forceStop() {
  // Call runInEventBaseThread() to perform all of the work in the actual
  // worker thread.
  //
  // This way we don't have to synchronize access to state_.
  eventBase_.runInEventBaseThread([this] {
    if (state_ == State::RUNNING || state_ == State::STOP_WHEN_IDLE) {
      state_ = State::FORCE_STOP;
      eventBase_.terminateLoopSoon();
    // state_ could be IDLE if we don't execute this callback until the
    // EventBase is destroyed in the WorkerThread destructor
    } else if (state_ != State::IDLE) {
      LOG(FATAL) << "forceStop() called in unexpected state " <<
          static_cast<int>(state_);
    }
  });
}

void WorkerThread::wait() {
  std::lock_guard<std::mutex> guard(joinLock_);
  if (thread_.joinable()) {
    thread_.join();
  }
}

void WorkerThread::setup() {
#ifndef _MSC_VER
  sigset_t ss;

  // Ignore some signals
  sigemptyset(&ss);
  sigaddset(&ss, SIGHUP);
  sigaddset(&ss, SIGINT);
  sigaddset(&ss, SIGQUIT);
  sigaddset(&ss, SIGUSR1);
  sigaddset(&ss, SIGUSR2);
  sigaddset(&ss, SIGPIPE);
  sigaddset(&ss, SIGALRM);
  sigaddset(&ss, SIGTERM);
  sigaddset(&ss, SIGCHLD);
  sigaddset(&ss, SIGIO);
  PCHECK(pthread_sigmask(SIG_BLOCK, &ss, nullptr) == 0);
#endif

  // Update the currentWorker_ thread-local pointer
  CHECK(nullptr == currentWorker_);
  currentWorker_ = this;

  // Update the manager with the event base this worker runs on
  if (eventBaseManager_) {
    eventBaseManager_->setEventBase(&eventBase_, false);
  }
}

void WorkerThread::cleanup() {
  currentWorker_ = nullptr;
  if (eventBaseManager_) {
    eventBaseManager_->clearEventBase();
  }
}

void WorkerThread::runLoop() {
  // Update state_
  CHECK(state_ == State::STARTING);
  state_ = State::RUNNING;

  VLOG(1) << "WorkerThread " << this << " starting";

  // Call loopForever().  This will only return after stopWhenIdle() or
  // forceStop() has been called.
  eventBase_.loopForever();

  if (state_ == State::STOP_WHEN_IDLE) {
    // We have been asked to stop when there are no more events left.
    // Call loop() to finish processing events.  This will return when there
    // are no more events to process, or after forceStop() has been called.
    VLOG(1) << "WorkerThread " << this << " finishing non-internal events";
    eventBase_.loop();
  }

  CHECK(state_ == State::STOP_WHEN_IDLE || state_ == State::FORCE_STOP);
  state_ = State::IDLE;

  VLOG(1) << "WorkerThread " << this << " terminated";
}

} // proxygen
