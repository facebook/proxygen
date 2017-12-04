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
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKContext.h>
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/HPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <proxygen/lib/http/codec/compress/test/TestStreamingCallback.h>
#include <folly/experimental/Bits.h>

using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;

class QPACKContextTests : public testing::TestWithParam<bool>,
                          public QPACKDecoder::Callback {
 public:
  void decodeStreaming(QPACKDecoder& decoder, const IOBuf* buf) {
    folly::io::Cursor c(buf);
    decoder.decodeStreaming(c, c.totalLength(), &cb);
  }

  void decodeControl(QPACKDecoder& decoder, const IOBuf* buf) {
    if (buf) {
      folly::io::Cursor c(buf);
      decoder.decodeControlStream(c, c.totalLength());
    }
  }

  void checkError(const IOBuf* buf, const HeaderDecodeError err);

  void ack(uint32_t index) override {
    deletedIndexes.push_back(index);
  }

  void onError() override {
    error = true;
  }

  TestStreamingCallback cb;
  std::list<uint32_t> deletedIndexes;
  bool error{false};
};

class TestQPACKContext : public QPACKContext {

 public:
  explicit TestQPACKContext(uint32_t tableSize) : QPACKContext(tableSize) {}

  bool add(const HPACKHeader& header, uint32_t index) {
    return table_.add(header, index);
  }
};

TEST_F(QPACKContextTests, get_index) {
  QPACKContext context(HPACK::kTableSize);
  HPACKHeader method(":method", "POST");

  // this will get it from the static table
  CHECK_EQ(context.getIndex(method), 3);
}

TEST_F(QPACKContextTests, is_static) {
  TestQPACKContext context(HPACK::kTableSize);
  // add 10 headers to the table
  for (int i = 1; i <= 10; i++) {
    HPACKHeader header("name" + folly::to<string>(i),
                       "value" + folly::to<string>(i));
    context.add(header, i);
  }
  EXPECT_EQ(context.getTable().size(), 10);

  EXPECT_EQ(context.isStatic(1), true);
  EXPECT_EQ(context.isStatic(10), true);
  EXPECT_EQ(context.isStatic(40), true);
  EXPECT_EQ(context.isStatic(60), true);
  EXPECT_EQ(context.isStatic(69), false);
}

TEST_F(QPACKContextTests, static_table) {
  auto& table = StaticHeaderTable::get();
  const HPACKHeader& first = table[1];
  const HPACKHeader& methodPost = table[3];
  const HPACKHeader& last = table[table.size()];
  // there are 61 entries in the spec
  CHECK_EQ(table.size(), 61);
  CHECK_EQ(methodPost, HPACKHeader(":method", "POST"));
  CHECK_EQ(first.name.get(), ":authority");
  CHECK_EQ(last.name.get(), "www-authenticate");
}

TEST_F(QPACKContextTests, static_index) {
  TestQPACKContext context(HPACK::kTableSize);
  HPACKHeader authority(":authority", "");
  context.getHeader(1).then([&] (QPACKHeaderTable::DecodeResult res) {
      EXPECT_EQ(res.ref, authority);
    });

  HPACKHeader post(":method", "POST");
  context.getHeader(3).then([&] (QPACKHeaderTable::DecodeResult res) {
      EXPECT_EQ(res.ref, post);
    });

  HPACKHeader contentLength("content-length", "");
  context.getHeader(28).then([&] (QPACKHeaderTable::DecodeResult res) {
      EXPECT_EQ(res.ref, contentLength);
    });
}

TEST_F(QPACKContextTests, encoder_multiple_values) {
  QPACKEncoder encoder(true);
  vector<HPACKHeader> req;
  req.push_back(HPACKHeader("accept-encoding", "gzip"));
  req.push_back(HPACKHeader("accept-encoding", "sdch,gzip"));
  auto encoded = encoder.encode(req);
  EXPECT_TRUE(encoded.second->length() > 0);
  EXPECT_EQ(encoder.getTable().size(), 2);
  QPACKDecoder decoder(*this);
  decodeControl(decoder, encoded.first.get());
  decodeStreaming(decoder, encoded.second.get());
  EXPECT_TRUE(cb.getResult().isOk());
  // sending the same request again should lead to a smaller but non
  // empty buffer
  auto encoded2 = encoder.encode(req);
  EXPECT_EQ(encoded2.first, nullptr);
  EXPECT_LT(encoded2.second->computeChainDataLength(),
            encoded.first->computeChainDataLength() +
            encoded.second->computeChainDataLength());
  EXPECT_GT(encoded2.second->computeChainDataLength(), 0);
  decodeControl(decoder, encoded2.first.get());
  decodeStreaming(decoder, encoded2.second.get());
  EXPECT_TRUE(cb.getResult().isOk());
}

TEST_F(QPACKContextTests, decoder_large_header) {
  // with this size basically the table will not be able to store any entry
  uint32_t size = 32;
  HPACKHeader header;
  QPACKEncoder encoder(true, size);
  QPACKDecoder decoder(*this, size);
  vector<HPACKHeader> headers;
  headers.push_back(HPACKHeader(":path", "verylargeheader"));
  // add a static entry
  headers.push_back(HPACKHeader(":method", "GET"));
  auto buf = encoder.encode(headers);
  decodeControl(decoder, buf.first.get());
  decodeStreaming(decoder, buf.second.get());
  EXPECT_EQ(encoder.getTable().size(), 0);
  EXPECT_EQ(decoder.getTable().size(), 0);
  EXPECT_TRUE(cb.getResult().isOk());
  EXPECT_EQ(cb.getResult().ok().headers.size(), 4);
}

#if 0
TEST_F(QPACKContextTests, eviction) {
  // with this size and 256 min free, 6x43 byte headers will trigger an
  // eviction
  uint32_t size = 512;
  HPACKHeader header;
  QPACKEncoder encoder(true, size);
  QPACKDecoder decoder(*this, size);
  vector<HPACKHeader> headers;
  for (int i = 1; i <= 6; i++) {
    headers.emplace_back("name" + folly::to<string>(i),
                         "value" + folly::to<string>(i));
  }
  auto buf = encoder.encode(headers);
  // there are 6 entries in the table, but one is marked invalid
  EXPECT_EQ(encoder.getTable().size(), 6);
  decodeControl(decoder, buf.first.get());
  decodeStreaming(decoder, buf.second.get());
  // after decode there are only five left
  EXPECT_EQ(decoder.getTable().size(), 5);
  EXPECT_TRUE(cb.getResult().isOk());
  EXPECT_EQ(cb.getResult().ok().headers.size(), headers.size() * 2);
  // there is one index ack pending
  EXPECT_EQ(deletedIndexes.size(), 1);
  std::array<uint8_t, 1> bits = {{0}};
  folly::Bits<uint8_t>::set(bits.begin(), deletedIndexes.front());
  auto ackBuf = folly::IOBuf::wrapBuffer(bits.begin(), bits.size());
  encoder.deleteAck(ackBuf.get());
  EXPECT_EQ(encoder.getTable().size(), 5);
}
#endif

/**
 * testing invalid memory access in the decoder; it has to always call peek()
 */
TEST_F(QPACKContextTests, decoder_invalid_peek) {
  QPACKEncoder encoder(true);
  QPACKDecoder decoder(*this);
  vector<HPACKHeader> headers;
  headers.push_back(HPACKHeader("x-fb-debug", "test"));

  auto encoded = encoder.encode(headers);
  unique_ptr<IOBuf> first = IOBuf::create(128);
  // set a trap for indexed header and don't call append
  first->writableData()[0] = HPACK::HeaderEncoding::INDEXED;

  first->appendChain(std::move(encoded.second));
  decodeControl(decoder, encoded.first.get());
  decodeStreaming(decoder, first.get());

  EXPECT_TRUE(cb.getResult().isOk());
  EXPECT_EQ(cb.getResult().ok().headers.size(), headers.size() * 2);
}

/**
 * similar with the one above, but slightly different code paths
 */
TEST_F(QPACKContextTests, decoder_invalid_literal_peek) {
  QPACKEncoder encoder(true);
  QPACKDecoder decoder(*this);
  vector<HPACKHeader> headers;
  headers.push_back(HPACKHeader("x-fb-random", "bla"));
  auto encoded = encoder.encode(headers);

  unique_ptr<IOBuf> first = IOBuf::create(128);
  first->writableData()[0] = 0x3F;

  first->appendChain(std::move(encoded.second));
  decodeControl(decoder, encoded.first.get());
  decodeStreaming(decoder, first.get());

  EXPECT_TRUE(cb.getResult().isOk());
  EXPECT_EQ(cb.getResult().ok().headers.size(), headers.size() * 2);
}

/**
 * testing various error cases in QPACKDecoder::decodeLiterHeader()
 */
void QPACKContextTests::checkError(const IOBuf* buf,
                                   const HeaderDecodeError err) {
  QPACKDecoder decoder(*this);
  decodeStreaming(decoder, buf);

  EXPECT_TRUE(cb.getResult().isError());
  EXPECT_EQ(cb.getResult().error(), err);
}

TEST_F(QPACKContextTests, decode_errors) {
  unique_ptr<IOBuf> buf = IOBuf::create(128);

  // 0. simulate an error decoding the index 0
  buf->writableData()[0] = 0x80;
  buf->append(1);  // intentionally omit the second byte
  checkError(buf.get(), HeaderDecodeError::INVALID_INDEX);

  // 1. simulate an error decoding the index for an indexed header name
  // we try to encode index 65
  buf->writableData()[0] = 0x0F;
  checkError(buf.get(), HeaderDecodeError::BUFFER_UNDERFLOW);

#if 0
  // qpack-05 has separate index ranges for static and dynamic
  // 2. invalid index (new index in static range
  buf->writableData()[0] = 0x41;
  checkError(buf.get(), HeaderDecodeError::INVALID_INDEX);
#endif

  // 3. buffer overflow when decoding literal header name
  buf->writableData()[0] = 0x00;  // this will activate the non-indexed branch
  checkError(buf.get(), HeaderDecodeError::BUFFER_UNDERFLOW);

  // 4. buffer overflow when decoding a header value
  // size for header name size and the actual header name
  buf->writableData()[1] = 0x01;
  buf->writableData()[2] = 'h';
  buf->append(2);
  checkError(buf.get(), HeaderDecodeError::BUFFER_UNDERFLOW);

  // 5. buffer overflow decoding the index of an indexed header
  buf->writableData()[0] = 0xFF; // first bit is 1 to mark indexed header
  buf->writableData()[1] = 0x80; // first bit is 1 to continue the
                                 // variable-length encoding
  buf->writableData()[2] = 0x80;
  checkError(buf.get(), HeaderDecodeError::BUFFER_UNDERFLOW);

#if 0
  // This doesn't do what we want
  // 6. Increase the table size
  buf->writableData()[0] = 0x3F;
  buf->writableData()[1] = 0xFF;
  buf->writableData()[2] = 0x7F;
  checkError(buf.get(), HeaderDecodeError::INVALID_TABLE_SIZE);
#endif

  // 7. integer overflow decoding the index of an indexed header
  buf->writableData()[0] = 0xFF; // first bit is 1 to mark indexed header
  buf->writableData()[1] = 0xFF;
  buf->writableData()[2] = 0xFF;
  buf->writableData()[3] = 0xFF;
  buf->writableData()[4] = 0xFF;
  buf->writableData()[5] = 0x7F;
  buf->append(3);
  checkError(buf.get(), HeaderDecodeError::INTEGER_OVERFLOW);
}

#if 0
// TABLE_SIZE_UPDATE is actually a QPACK delete instruction, so this test
// doesn't do what it claims.
TEST_F(QPACKContextTests, contextUpdate) {
  // using hpack to generate a frame with a table size update in it
  HPACKEncoder encoder(true);
  QPACKDecoder decoder(*this);
  vector<HPACKHeader> headers;
  encoder.setHeaderTableSize(8192);
  headers.push_back(HPACKHeader("x-fb-random", "bla"));
  auto encoded = encoder.encode(headers);

  unique_ptr<IOBuf> first = IOBuf::create(128);

  first->appendChain(std::move(encoded.second));
  decodeStreaming(decoder, first.get());

  EXPECT_TRUE(cb.getResult().isError());
  EXPECT_EQ(cb.getResult().error(), HeaderDecodeError::INVALID_TABLE_SIZE);
}
#endif
