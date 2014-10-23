/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <stdint.h>

#include "folly/experimental/wangle/acceptor/TransportInfo.h"

namespace proxygen {

typedef folly::HTTPHeaderSize HTTPHeaderSize;

// /**
//  * A structure that encapsulates byte counters related to the HTTP headers.
//  */
// struct HTTPHeaderSize {
//   /**
//    * The number of bytes used to represent the header after compression or
//    * before decompression. If header compression is not supported, the value
//    * is set to 0.
//    */
//   uint32_t compressed{0};

//   /**
//    * The number of bytes used to represent the serialized header before
//    * compression or after decompression, in plain-text format.
//    */
//   uint32_t uncompressed{0};
// };

}
