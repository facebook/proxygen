/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ZlibStreamCompressor.h"

#include <folly/Bits.h>
#include <folly/io/Cursor.h>

using namespace folly;
using folly::IOBuf;
using std::unique_ptr;

// IOBuf uses 24 bytes of data for bookeeping purposes, so requesting for 4073
// bytes of data will be rounded up to an allocation of 1 page.
DEFINE_int64(zlib_compressor_buffer_growth, 2024,
             "The buffer growth size to use during IOBuf zlib deflation");
DEFINE_int64(zlib_compressor_buffer_minsize, 1024,
      "The minimum buffer size to use before growing during IOBuf "
      "zlib deflation");

namespace proxygen {

void ZlibStreamCompressor::init(ZlibCompressionType type, int32_t level) {

  DCHECK(type_ == ZlibCompressionType::NONE)
    << "Attempt to re-initialize compression stream";

  type_ = type;
  level_ = level;
  status_ = Z_OK;

  zlibStream_.zalloc = Z_NULL;
  zlibStream_.zfree = Z_NULL;
  zlibStream_.opaque = Z_NULL;
  zlibStream_.total_in = 0;
  zlibStream_.next_in = Z_NULL;
  zlibStream_.avail_in = 0;
  zlibStream_.avail_out = 0;
  zlibStream_.next_out = Z_NULL;

  DCHECK(level_ >= Z_NO_COMPRESSION && level_ <= Z_BEST_COMPRESSION)
    << "Invalid Zlib compression level. level=" << level_;

  switch (type_) {
    case ZlibCompressionType::GZIP:
      status_ = deflateInit2(&zlibStream_,
                            level_,
                            Z_DEFLATED,
                            static_cast<int32_t>(type),
                            MAX_MEM_LEVEL,
                            Z_DEFAULT_STRATEGY);
      break;
    case ZlibCompressionType::DEFLATE:
      status_ = deflateInit(&zlibStream_, level);
      break;
    default:
      DCHECK(false) << "Unsupported zlib compression type.";
      break;
  }

  if (status_ != Z_OK) {
      LOG(ERROR) << "error initializing zlib stream. r=" << status_ ;
  }
}

ZlibStreamCompressor::ZlibStreamCompressor(ZlibCompressionType type, int level)
    : status_(Z_OK) {
  init(type, level);
}

ZlibStreamCompressor::~ZlibStreamCompressor() {
  if (type_ != ZlibCompressionType::NONE) {
    status_ = deflateEnd(&zlibStream_);
  }
}

// Compress an IOBuf chain. Compress can be called multiple times and the
// Zlib stream will be synced after each call. trailer must be set to
// true on the final compression call.
std::unique_ptr<IOBuf> ZlibStreamCompressor::compress(const IOBuf* in,
                                                      bool trailer) {

  const IOBuf* crtBuf{in};
  size_t offset{0};
  int flush{Z_NO_FLUSH};

  auto out = IOBuf::create(FLAGS_zlib_compressor_buffer_growth);
  auto appender = folly::io::Appender(out.get(),
      FLAGS_zlib_compressor_buffer_growth);

  auto chunkSize = FLAGS_zlib_compressor_buffer_minsize;

  do {
    // Advance to the next IOBuf if necessary
    DCHECK_GE(crtBuf->length(), offset);
    if (crtBuf->length() == offset) {
      crtBuf = crtBuf->next();
      if (crtBuf == in) {
        // We hit the end of the IOBuf chain, and are done.
        // Need to flush the stream
        // Completely flush if the final call, otherwise flush state
        if (trailer) {
          flush = Z_FINISH;
        } else {
          flush = Z_SYNC_FLUSH;
        }
      } else {
        // Prepare to process next buffer
        offset = 0;
      }
    }

    if (status_ == Z_STREAM_ERROR) {
      LOG(ERROR) << "error compressing buffer. r=" << status_;
      return nullptr;
    }

    const size_t origAvailIn = crtBuf->length() - offset;

    zlibStream_.next_in = const_cast<uint8_t*>(crtBuf->data() + offset);
    zlibStream_.avail_in = origAvailIn;
    // Zlib may not write it's entire state on the first pass.
    do {
      appender.ensure(chunkSize);

      zlibStream_.next_out = appender.writableData();
      zlibStream_.avail_out = chunkSize;
      status_ = deflate(&zlibStream_, flush);

      // Move output buffer ahead
      auto outMove = chunkSize - zlibStream_.avail_out;
      appender.append(outMove);
    } while (zlibStream_.avail_out == 0);
    DCHECK_EQ(zlibStream_.avail_in, 0);

    // Adjust the input offset ahead
    auto inConsumed = origAvailIn - zlibStream_.avail_in;
    offset += inConsumed;

  } while (flush != Z_FINISH && flush != Z_SYNC_FLUSH);

  return out;
}
}
