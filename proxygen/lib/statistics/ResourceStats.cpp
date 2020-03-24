/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "proxygen/lib/statistics/ResourceStats.h"

namespace proxygen {

ResourceStats::ResourceStats(std::unique_ptr<Resources> resources)
    : PeriodicStats<ResourceData>(
          new ResourceData(resources->getCurrentData())),
      resources_(std::move(resources)) {
}

ResourceData* ResourceStats::getNewData() const {
  return new ResourceData(resources_->getCurrentData());
}

} // namespace proxygen
