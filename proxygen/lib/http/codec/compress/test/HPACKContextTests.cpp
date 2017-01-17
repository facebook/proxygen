/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Conv.h>
#include <glog/logging.h>
#include <folly/portability/GTest.h>
#include <memory>
#include <proxygen/lib/http/codec/compress/HPACKContext.h>
#include <proxygen/lib/http/codec/compress/HPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/HPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/Logging.h>

using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;

class HPACKContextTests : public testing::TestWithParam<bool> {
};

class TestContext : public HPACKContext {

 public:
  explicit TestContext(uint32_t tableSize) : HPACKContext(tableSize) {}

  void add(const HPACKHeader& header) {
    table_.add(header);
  }

};

TEST_F(HPACKContextTests, get_index) {
  HPACKContext context(HPACK::kTableSize);
  HPACKHeader method(":method", "POST");

  // this will get it from the static table
  CHECK_EQ(context.getIndex(method), 3);
}

TEST_F(HPACKContextTests, is_static) {
  TestContext context(HPACK::kTableSize);
  // add 10 headers to the table
  for (int i = 1; i <= 10; i++) {
    HPACKHeader header("name" + folly::to<string>(i),
                      "value" + folly::to<string>(i));
    context.add(header);
  }
  EXPECT_EQ(context.getTable().size(), 10);


  EXPECT_EQ(context.isStatic(1), true);
  EXPECT_EQ(context.isStatic(10), true);
  EXPECT_EQ(context.isStatic(40), true);
  EXPECT_EQ(context.isStatic(60), true);
  EXPECT_EQ(context.isStatic(69), false);
}

TEST_F(HPACKContextTests, static_table) {
  auto& table = StaticHeaderTable::get();
  const HPACKHeader& first = table[1];
  const HPACKHeader& methodPost = table[3];
  const HPACKHeader& last = table[table.size()];
  // there are 61 entries in the spec
  CHECK_EQ(table.size(), 61);
  CHECK_EQ(table[3], HPACKHeader(":method", "POST"));
  CHECK_EQ(table[1].name, ":authority");
  CHECK_EQ(table[table.size()].name, "www-authenticate");
}

TEST_F(HPACKContextTests, static_index) {
  TestContext context(HPACK::kTableSize);
  HPACKHeader authority(":authority", "");
  EXPECT_EQ(context.getHeader(1), authority);

  HPACKHeader post(":method", "POST");
  EXPECT_EQ(context.getHeader(3), post);

  HPACKHeader contentLength("content-length", "");
  EXPECT_EQ(context.getHeader(28), contentLength);
}

TEST_F(HPACKContextTests, encoder_multiple_values) {
  HPACKEncoder encoder(true);
  vector<HPACKHeader> req;
  req.push_back(HPACKHeader("accept-encoding", "gzip"));
  req.push_back(HPACKHeader("accept-encoding", "sdch,gzip"));
  // with the first encode both headers should be in the reference set
  unique_ptr<IOBuf> encoded = encoder.encode(req);
  EXPECT_TRUE(encoded->length() > 0);
  EXPECT_EQ(encoder.getTable().size(), 2);
  // sending the same request again should lead to a smaller but non
  // empty buffer
  unique_ptr<IOBuf> encoded2 = encoder.encode(req);
  EXPECT_LT(encoded2->computeChainDataLength(),
            encoded->computeChainDataLength());
  EXPECT_GT(encoded2->computeChainDataLength(), 0);
}

TEST_F(HPACKContextTests, decoder_large_header) {
  // with this size basically the table will not be able to store any entry
  uint32_t size = 32;
  HPACKHeader header;
  HPACKEncoder encoder(true, size);
  HPACKDecoder decoder(size);
  vector<HPACKHeader> headers;
  headers.push_back(HPACKHeader(":path", "verylargeheader"));
  // add a static entry
  headers.push_back(HPACKHeader(":method", "GET"));
  auto buf = encoder.encode(headers);
  auto decoded = decoder.decode(buf.get());
  EXPECT_EQ(encoder.getTable().size(), 0);
  EXPECT_EQ(encoder.getTable().referenceSet().size(), 0);
  EXPECT_EQ(decoder.getTable().size(), 0);
  EXPECT_EQ(decoder.getTable().referenceSet().size(), 0);
}

/**
 * testing invalid memory access in the decoder; it has to always call peek()
 */
TEST_F(HPACKContextTests, decoder_invalid_peek) {
  HPACKEncoder encoder(true);
  HPACKDecoder decoder;
  vector<HPACKHeader> headers;
  headers.push_back(HPACKHeader("x-fb-debug", "test"));

  unique_ptr<IOBuf> encoded = encoder.encode(headers);
  unique_ptr<IOBuf> first = IOBuf::create(128);
  // set a trap for indexed header and don't call append
  first->writableData()[0] = HPACK::HeaderEncoding::INDEXED;

  first->appendChain(std::move(encoded));
  auto decoded = decoder.decode(first.get());

  EXPECT_FALSE(decoder.hasError());
  EXPECT_EQ(*decoded, headers);
}

/**
 * similar with the one above, but slightly different code paths
 */
TEST_F(HPACKContextTests, decoder_invalid_literal_peek) {
  HPACKEncoder encoder(true);
  HPACKDecoder decoder;
  vector<HPACKHeader> headers;
  headers.push_back(HPACKHeader("x-fb-random", "bla"));
  unique_ptr<IOBuf> encoded = encoder.encode(headers);

  unique_ptr<IOBuf> first = IOBuf::create(128);
  first->writableData()[0] = 0x3F;

  first->appendChain(std::move(encoded));
  auto decoded = decoder.decode(first.get());

  EXPECT_FALSE(decoder.hasError());
  EXPECT_EQ(*decoded, headers);
}

/**
 * testing various error cases in HPACKDecoder::decodeLiterHeader()
 */
void checkError(const IOBuf* buf, const HPACK::DecodeError err) {
  HPACKDecoder decoder;
  auto decoded = decoder.decode(buf);
  EXPECT_TRUE(decoder.hasError());
  EXPECT_EQ(decoder.getError(), err);
}

TEST_F(HPACKContextTests, decode_errors) {
  unique_ptr<IOBuf> buf = IOBuf::create(128);

  // 1. simulate an error decoding the index for an indexed header name
  // we try to encode index 65
  buf->writableData()[0] = 0x3F;
  buf->append(1);  // intentionally omit the second byte
  checkError(buf.get(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // 2. invalid index for indexed header name
  buf->writableData()[0] = 0x7F;
  buf->writableData()[1] = 0xFF;
  buf->writableData()[2] = 0x7F;
  buf->append(2);
  checkError(buf.get(), HPACK::DecodeError::INVALID_INDEX);

  // 3. buffer overflow when decoding literal header name
  buf->writableData()[0] = 0x00;  // this will activate the non-indexed branch
  checkError(buf.get(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // 4. buffer overflow when decoding a header value
  // size for header name size and the actual header name
  buf->writableData()[1] = 0x01;
  buf->writableData()[2] = 'h';
  checkError(buf.get(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // 5. buffer overflow decoding the index of an indexed header
  buf->writableData()[0] = 0xFF; // first bit is 1 to mark indexed header
  buf->writableData()[1] = 0x80; // first bit is 1 to continue the
                                 // variable-length encoding
  buf->writableData()[2] = 0x80;
  checkError(buf.get(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // 6. Increase the table size
  buf->writableData()[0] = 0x3F;
  buf->writableData()[1] = 0xFF;
  buf->writableData()[2] = 0x7F;
  checkError(buf.get(), HPACK::DecodeError::INVALID_TABLE_SIZE);

  // 7. integer overflow decoding the index of an indexed header
  buf->writableData()[0] = 0xFF; // first bit is 1 to mark indexed header
  buf->writableData()[1] = 0xFF;
  buf->writableData()[2] = 0xFF;
  buf->writableData()[3] = 0xFF;
  buf->writableData()[4] = 0xFF;
  buf->writableData()[5] = 0x7F;
  buf->append(3);
  checkError(buf.get(), HPACK::DecodeError::INTEGER_OVERFLOW);
}

TEST_P(HPACKContextTests, contextUpdate) {
  HPACKEncoder encoder(true);
  HPACKDecoder decoder;
  vector<HPACKHeader> headers;
  bool setDecoderSize = GetParam();
  encoder.setHeaderTableSize(8192);
  if (setDecoderSize) {
    decoder.setHeaderTableMaxSize(8192);
  }
  headers.push_back(HPACKHeader("x-fb-random", "bla"));
  unique_ptr<IOBuf> encoded = encoder.encode(headers);

  unique_ptr<IOBuf> first = IOBuf::create(128);

  first->appendChain(std::move(encoded));
  auto decoded = decoder.decode(first.get());


  EXPECT_EQ(decoder.hasError(), !setDecoderSize);
  if (setDecoderSize) {
    EXPECT_EQ(*decoded, headers);
  } else {
    EXPECT_EQ(decoder.getError(), HPACK::DecodeError::INVALID_TABLE_SIZE);
  }
}

INSTANTIATE_TEST_CASE_P(Context,
                        HPACKContextTests,
                        ::testing::Values(true, false));
