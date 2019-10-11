/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "proxygen/lib/statistics/ResourceStats.h"

#include <folly/synchronization/Rcu.h>
#include <glog/logging.h>

namespace proxygen {

ResourceStats::ResourceStats(std::unique_ptr<Resources> resources)
    : resources_(std::move(resources)),
      data_(new ResourceData(resources_->getCurrentData())) {
}

ResourceStats::~ResourceStats() {
  stopRefresh();
  modifyData(nullptr);
}

void ResourceStats::refreshWithPeriod(std::chrono::milliseconds periodMs) {
  CHECK_GE(periodMs.count(), 0);
  std::lock_guard<std::mutex> guard(schedulerMutex_);
  refreshPeriodMs_ = periodMs;
  if (!scheduler_) {
    scheduler_.reset(new folly::FunctionScheduler());
    scheduler_->setThreadName("sys_stats");
    // Steady here implies that scheduling will be fixed as opposed to
    // offsetting from the current time which is desired to ensure minimal
    // use of synchronization for getCurrentLoadData()
    scheduler_->setSteady(true);

    std::function<void()> updateFunc(
        std::bind(&ResourceStats::updateCachedData, this));
    std::function<std::chrono::milliseconds()> intervalFunc(
        std::bind(&ResourceStats::getRefreshIntervalMs, this));

    scheduler_->addFunctionGenericDistribution(updateFunc,
                                               intervalFunc,
                                               "sys_stats",
                                               "sys_stats_interval",
                                               std::chrono::milliseconds(0));

    scheduler_->start();
  }
}

void ResourceStats::stopRefresh() {
  std::lock_guard<std::mutex> guard(schedulerMutex_);
  scheduler_.reset();
}

const ResourceData& ResourceStats::getCurrentLoadData() const {
  {
    folly::rcu_reader guard;
    auto* loadedData = data_.load();
    if (loadedData->getLastUpdateTime() != tlData_->getLastUpdateTime()) {
      // Should be fine using the default assignment operator the compiler
      // gave us I think...this will stop being true if loadedData starts
      // storing pointers.
      *tlData_ = *loadedData;
    }
  }
  return *tlData_;
}

const ResourceData& ResourceStats::getPreviousLoadData() const {
  return *tlData_;
}

void ResourceStats::updateCachedData() {
  modifyData(new ResourceData(resources_->getCurrentData()));
}

void ResourceStats::modifyData(ResourceData* newData) {
  auto* oldData = data_.load();
  // Default copy constructor should be fine here much as above...this will
  // stop being true if data starts storing pointers.
  data_.store(newData);
  folly::rcu_retire(oldData);
}

} // namespace proxygen
