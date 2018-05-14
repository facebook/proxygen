/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/HPACKEncoderBase.h>

namespace proxygen {

void HPACKEncoderBase::handlePendingContextUpdate(HPACKEncodeBuffer& buf,
                                                  uint32_t tableCapacity) {
  CHECK_EQ(HPACK::TABLE_SIZE_UPDATE.code, HPACK::Q_TABLE_SIZE_UPDATE.code) <<
    "Code assumes these are equal";
  if (pendingContextUpdate_) {
    VLOG(5) << "Encoding table size update size=" << tableCapacity;
    buf.encodeInteger(tableCapacity, HPACK::TABLE_SIZE_UPDATE);
    pendingContextUpdate_ = false;
  }
}

}
