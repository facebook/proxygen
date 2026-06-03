/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/compress/HPACKEncoderBase.h>
#include <proxygen/lib/utils/LogShim.h>

namespace proxygen {

uint32_t HPACKEncoderBase::handlePendingContextUpdate(HPACKEncodeBuffer& buf,
                                                      uint32_t tableCapacity) {
  PRX_CHECK_EQ(HPACK::TABLE_SIZE_UPDATE.code, HPACK::Q_TABLE_SIZE_UPDATE.code)
      << "Code assumes these are equal";
  uint32_t encoded = 0;
  if (pendingContextUpdate_) {
    PRX_VLOG(5) << "Encoding table size update size=" << tableCapacity;
    encoded = buf.encodeInteger(tableCapacity, HPACK::TABLE_SIZE_UPDATE);
    pendingContextUpdate_ = false;
  }

  return encoded;
}

} // namespace proxygen
