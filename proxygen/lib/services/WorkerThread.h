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

#include <atomic>
#include <cstdint>
#include <folly/Portability.h>
#include <folly/io/async/EventBase.h>
#include <mutex>
#include <thread>

namespace folly {
class EventBaseManager;
}

namespace proxygen {

/**
 * A WorkerThread represents an independent event loop that runs in its own
 * thread.
 */
class WorkerThread {
 public:
  explicit WorkerThread(folly::EventBaseManager* ebm);
  virtual ~WorkerThread();

  /**
   * Begin execution of the worker.
   *
   * This starts the worker thread, and returns immediately.
   */
  void start();

  /**
   * Request that the worker thread stop when there are no more events to
   * process.
   *
   * Normally each worker thread runs forever, even if it is idle with no
   * events to process.  This function requests that the worker thread return
   * when it becomes idle.
   *
   * This is used for graceful shutdown: Once the services have been asked to
   * shutdown, stopWhenIdle() can be called on the WorkerThread so that it will
   * return as soon as the services in this thread no longer have any events to
   * process.
   *
   * Typically you will still want to call forceStop() after a timeout, in case
   * some of the services take too long to shut down gracefully.
   */
  void stopWhenIdle();

  /**
   * Request that the worker stop executing as soon as possible.
   *
   * This will terminate the worker thread's event loop, and cause the thread
   * to return.  If there are any services still running in the worker thread,
   * their events will no longer be processed.
   *
   * This function is asynchronous: it signals the worker thread to stop, and
   * returns without waiting for the thread to actually terminate.  The wait()
   * method must be called to wait for the thread to terminate.
   */
  void forceStop();

  /**
   * Synchronously wait for termination of the worker thread.
   *
   * Note that the worker thread will only terminate after stopWhenIdle() or
   * forceStop() has been called, so you typically should only call wait()
   * after first using one of these functions.
   */
  void wait();

  /**
   * Get the EventBase used to drive the events in this worker thread.
   */
  folly::EventBase* getEventBase() {
    return &eventBase_;
  }

  /**
   * Get the current WorkerThread running this thread.
   *
   * Returns nullptr if called from a thread that is not running inside
   * WorkerThread.
   */
  static WorkerThread* getCurrentWorkerThread() {
    return currentWorker_;
  }

 protected:
  virtual void setup();
  virtual void cleanup();

 private:
  enum class State : uint8_t {
    IDLE,           // Not yet started
    STARTING,       // start() called, thread not fully started yet
    RUNNING,        // Thread running normally
    STOP_WHEN_IDLE, // stopWhenIdle() called, not stopped yet
    FORCE_STOP,     // forceStop() called, but the loop is still cleaning up
  };

  // Forbidden copy constructor and assignment operator
  WorkerThread(WorkerThread const &) = delete;
  WorkerThread& operator=(WorkerThread const &) = delete;

  void runLoop();

  State state_{State::IDLE};
  std::thread thread_;
  std::mutex joinLock_;
  folly::EventBase eventBase_;
  folly::EventBaseManager* eventBaseManager_{nullptr};

  // A thread-local pointer to the current WorkerThread for this thread
  static FOLLY_TLS WorkerThread* currentWorker_;

  // A count of the number of WorkerThreads that have been constructed
  static std::atomic_uint objectCounter_;
};

} // proxygen
