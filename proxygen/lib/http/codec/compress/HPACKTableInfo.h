/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <stdint.h>

namespace proxygen {

/*
 * Struct to hold the encoder and decoder information
 */
struct HPACKTableInfo {
  // Egress table info (encoder)
  uint32_t egressHeaderTableSize_{0};
  uint32_t egressBytesStored_{0};
  uint32_t egressHeadersStored_{0};

  // Ingress table info (decoder)
  uint32_t ingressHeaderTableSize_{0};
  uint32_t ingressBytesStored_{0};
  uint32_t ingressHeadersStored_{0};

  HPACKTableInfo(uint32_t egressHeaderTableSize,
                 uint32_t egressBytesStored,
                 uint32_t egressHeadersStored,
                 uint32_t ingressHeaderTableSize,
                 uint32_t ingressBytesStored,
                 uint32_t ingressHeadersStored) :
      egressHeaderTableSize_(egressHeaderTableSize),
      egressBytesStored_(egressBytesStored),
      egressHeadersStored_(egressHeadersStored),
      ingressHeaderTableSize_(ingressHeaderTableSize),
      ingressBytesStored_(ingressBytesStored),
      ingressHeadersStored_(ingressHeadersStored) {}

  HPACKTableInfo() {}

  bool operator==(const HPACKTableInfo& tableInfo) const {
    return egressHeaderTableSize_ == tableInfo.egressHeaderTableSize_ &&
           egressBytesStored_ == tableInfo.egressBytesStored_ &&
           egressHeadersStored_ == tableInfo.egressHeadersStored_ &&
           ingressHeaderTableSize_ == tableInfo.ingressHeaderTableSize_ &&
           ingressBytesStored_ == tableInfo.ingressBytesStored_ &&
           ingressHeadersStored_ == tableInfo.ingressHeadersStored_;
  }
};
} // namespace proxygen
