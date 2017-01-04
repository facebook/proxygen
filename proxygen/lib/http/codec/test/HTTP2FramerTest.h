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

#include <folly/io/IOBufQueue.h>

// Writes out the common frame header without checks
void writeFrameHeaderManual(folly::IOBufQueue& queue,
                            uint32_t length, uint8_t type,
                            uint8_t flags, uint32_t stream);
