/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "proxygen/lib/statistics/ResourceData.h"
#include <atomic>
#include <chrono>
#include <folly/ThreadLocal.h>
#include <folly/experimental/FunctionScheduler.h>

namespace proxygen {

/**
 * ResourceStats:
 *
 * A class designed to abstract away the internals of retrieving
 * various resource utilization metrics.  The main goal is to provide
 * a thread-safe mechanism for retrieving desired information (i.e. multiple
 * readers, single writer).  In this way readers view a consistent
 * representation of the underlying data, as opposed to a model where each
 * reader fetches the data separately, potentially at different times, and thus
 * potentially coming to different conclusions (not to mention wasting
 * resources performing the same operations).  Also consider the future
 * extensibility potential especially in the context of second order stats and
 * counter aggregation.
 *
 * All public methods are thread-safe (incl cache coherence).
 */
class ResourceStats {
 public:
  /**
   * Default constructor that will initialize data_ as appropriate.
   * Note: as CPU pct utilization requires intervals and at init time there
   * is only a single data point, pct utilization is initially seeded from
   * proc loadavg.  Underlying thread that updates locally cached Data is not
   * started yet.
   */
  explicit ResourceStats(std::unique_ptr<Resources> resources);

  virtual ~ResourceStats();

  // Returns the current refresh interval in ms of the underlying data.
  std::chrono::milliseconds getRefreshIntervalMs() {
    return refreshPeriodMs_;
  }

  /**
   * Refreshes the underlying resource utilization data using the period
   * specified by employing the underlying function scheduler which will
   * subsequently create a thread.  If already refreshing, the current
   * period will instead be updated and on next scheduling the updated period
   * applied.
   */
  void refreshWithPeriod(std::chrono::milliseconds periodMs);

  /**
   * Stops refreshing the underlying resource utilization data.  It is
   * possible that an update is occuring as this method is run in which case
   * the guarantee becomes that no subsequent refresh will be scheduled.
   */
  void stopRefresh();

  /**
   * Readers utilize this method to get their thread local representation
   * of the current resource utilization metrics.  The way in which this works
   * is that while there is a thread local representation of the metrics,
   * there is also a global representation that is guarded.  Asynchronously,
   * the refresh thread will update the global representation in a thread safe
   * manner.  Then, as readers wish to consume this data, they are instead
   * presented with a thread local representation that is updated if:
   *  A) their representation is uninitialized
   *  B) their representation is old
   * Note for case (B) there is no guarantee that a newer version is
   * actually available (as is the way with threading).  Such situations
   * should only occur if the driving thread is backlogged or if one or more
   * stray readers are holding onto the mutex for some reason but regardless
   * performance is NOT severely degraded; a read lock acquire and a
   * fundamental type check is all that is performed.
   *
   * Method is virtual for testing reasons.
   */
  virtual const ResourceData& getCurrentLoadData() const;
  // Same as above except no local update will be performed, even if newer
  // data is available.
  virtual const ResourceData& getPreviousLoadData() const;

 protected:
  void updateCachedData();

  /**
   * Abstraction that enables callers to provide their own implementations
   * of the entity that actually queries various metrics.
   */
  std::unique_ptr<Resources> resources_;

  /**
   * data_ represents the source of truth for the various resource fields.
   * tlData_ is updated on a 'as-used' basis from data via RCU
   * synchronization.
   */
  std::atomic<ResourceData*> data_;
  folly::ThreadLocal<ResourceData> tlData_;

  // Refresh management fields

  /**
   * scheduler_ represents the underlying abstraction that will create
   * the thread that updates stats asynchronously.
   *
   * The mutex specifically synchronizes access to scheduler_.  It is
   * leveraged to ensure all public APIs are thread-safe.
   *
   * The refreshPeriodMs_ is the amount of time that must elapse before
   * the underlying cached data is updated.  It is atomic as it is read
   * under the dataMutex_ but can also be read/set under schedulerMutex_.
   */
  std::unique_ptr<folly::FunctionScheduler> scheduler_;
  std::mutex schedulerMutex_;
  std::atomic<std::chrono::milliseconds> refreshPeriodMs_{
      std::chrono::milliseconds(0)};

 private:
  // Wrapper for updating and retiring old cached data_ via RCU.
  void modifyData(ResourceData* newData);
};

} // namespace proxygen
