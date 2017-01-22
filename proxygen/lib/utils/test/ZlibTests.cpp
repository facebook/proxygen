/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/IOBuf.h>
#include <folly/io/Cursor.h>
#include <folly/Random.h>
#include <glog/logging.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/utils/ZlibStreamCompressor.h>
#include <proxygen/lib/utils/ZlibStreamDecompressor.h>

using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;

class ZlibTests : public testing::Test {
};

std::unique_ptr<folly::IOBuf> makeBuf(uint32_t size) {
  auto out = folly::IOBuf::create(size);
  out->append(size);
  // fill with random junk
  folly::io::RWPrivateCursor cursor(out.get());
  while (cursor.length() >= 8) {
    cursor.write<uint64_t>(folly::Random::rand64());
  }
  while (cursor.length()) {
    cursor.write<uint8_t>((uint8_t)folly::Random::rand32());
  }
  return out;
}

void compressThenDecompress(ZlibCompressionType type,
                            int level,
                            const unique_ptr<IOBuf> buf) {

  unique_ptr<IOBuf> compressed;
  unique_ptr<IOBuf> decompressed;

  unique_ptr<ZlibStreamCompressor> zc(new ZlibStreamCompressor(type, level));
  unique_ptr<ZlibStreamDecompressor> zd(new ZlibStreamDecompressor(type));

  compressed = zc->compress(buf.get(), true);
  ASSERT_FALSE(zc->hasError()) << "Compression error. r=" << zc->getStatus();

  decompressed = zd->decompress(compressed.get());
  ASSERT_FALSE(zd->hasError()) << "Decompression error. r=" << zd->getStatus();

  IOBufEqual eq;
  ASSERT_TRUE(eq(buf, decompressed));
}

// Try many different sizes because we've hit truncation problems before
TEST_F(ZlibTests, compress_decompress_gzip_5000) {
  ASSERT_NO_FATAL_FAILURE({
    compressThenDecompress(ZlibCompressionType::GZIP, 6, makeBuf(5000));
  });
}

TEST_F(ZlibTests, compress_decompress_gzip_2000) {
  ASSERT_NO_FATAL_FAILURE({
    compressThenDecompress(ZlibCompressionType::GZIP, 6, makeBuf(2000));
  });
}

TEST_F(ZlibTests, compress_decompress_gzip_1024) {
  ASSERT_NO_FATAL_FAILURE({
    compressThenDecompress(ZlibCompressionType::GZIP, 6, makeBuf(1024));
  });
}

TEST_F(ZlibTests, compress_decompress_gzip_500) {
  ASSERT_NO_FATAL_FAILURE({
    compressThenDecompress(ZlibCompressionType::GZIP, 6, makeBuf(500));
  });
}

TEST_F(ZlibTests, compress_decompress_gzip_50) {
  ASSERT_NO_FATAL_FAILURE({
    compressThenDecompress(ZlibCompressionType::GZIP, 6, makeBuf(50));
  });
}

TEST_F(ZlibTests, compress_decompress_deflate) {
  ASSERT_NO_FATAL_FAILURE({
    compressThenDecompress(ZlibCompressionType::DEFLATE, 6, makeBuf(500));
  });
}

TEST_F(ZlibTests, compress_decompress_empty) {
  ASSERT_NO_FATAL_FAILURE({
    compressThenDecompress(ZlibCompressionType::GZIP, 4, makeBuf(0));
  });
}
