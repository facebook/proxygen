/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/utils/BrotliStreamDecompressor.h>

#include <folly/io/Cursor.h>

static const size_t kFileBufferSize = 65536;

namespace proxygen {

BrotliStreamDecompressor::BrotliStreamDecompressor() {
  state_ = BrotliCreateState(NULL, NULL, NULL);
  status_ = BrotliStatusType::NONE;
}

BrotliStreamDecompressor::~BrotliStreamDecompressor() {
  BrotliDestroyState(state_);
}

std::unique_ptr<folly::IOBuf> BrotliStreamDecompressor::decompress(const folly::IOBuf* in) {
  if (!state_) {
    status_ = BrotliStatusType::ERROR;
  }

  // TODO: Find a reasonable value
  auto out = folly::IOBuf::create(1024);
  auto appender = folly::io::Appender(out.get(),
      1024);

  const folly::IOBuf* crtBuf = in;
  size_t offset = 0;
  size_t total_out = 0;
  BrotliResult result = BROTLI_RESULT_NEEDS_MORE_INPUT;
  while (true) {
    // Ensure there is space in the output IOBuf
    appender.ensure(crtBuf->length()*2);
    if (result == BROTLI_RESULT_NEEDS_MORE_INPUT) {
      if (crtBuf->length() == offset) {
        crtBuf = crtBuf->next();
        offset = 0;
        if (crtBuf == in) {
          // We're done due to full circular buffer loop
          break;
        }
      }
    } else if (result == BROTLI_RESULT_ERROR ||
               result == BROTLI_RESULT_SUCCESS) {
      break;
    }

    size_t origAvailIn = crtBuf->length() - offset;
    const uint8_t* next_in = const_cast<uint8_t*>(crtBuf->data() + offset);
    size_t avail_in = origAvailIn;
    uint8_t* next_out = appender.writableData();
    size_t avail_out = appender.length();

    result = BrotliDecompressStream(&avail_in,
                                    &next_in,
                                    &avail_out,
                                    &next_out,
                                    &total_out,
                                    state_);

    // Adjust the input offset ahead
    auto inConsumed = origAvailIn - avail_in;
    offset += inConsumed;

    // Move output buffer ahead
    auto outMove = appender.length() - avail_out;
    appender.append(outMove);
  }

  if (result == BROTLI_RESULT_NEEDS_MORE_INPUT) {
    status_ = BrotliStatusType::CONTINUE;
    return out;
  } else if ((result == BROTLI_RESULT_NEEDS_MORE_OUTPUT) ||
      (result != BROTLI_RESULT_SUCCESS)) {
    status_ = BrotliStatusType::ERROR;
    return nullptr;
  }

  status_ = BrotliStatusType::SUCCESS;
  return out;
}

}
