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
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMContext.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMDecoder.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMEncoder.h>
#include <proxygen/lib/http/codec/compress/HPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <proxygen/lib/http/codec/compress/test/TestStreamingCallback.h>
#include <folly/experimental/Bits.h>

using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;

class QCRAMContextTests : public testing::TestWithParam<bool>,
                          public QCRAMDecoder::Callback {
 public:
  void decodeStreaming(QCRAMDecoder& decoder, const IOBuf* buf) {
    folly::io::Cursor c(buf);
    decoder.decodeStreaming(c, c.totalLength(), &cb);
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

class TestQCRAMContext : public QCRAMContext {

 public:
  explicit TestQCRAMContext(uint32_t tableSize) : QCRAMContext(tableSize) {}

  bool add(const HPACKHeader& header, uint32_t index) {
    return table_.add(header, index);
  }
};

TEST_F(QCRAMContextTests, get_index) {
  QCRAMContext context(HPACK::kTableSize);
  HPACKHeader method(":method", "POST");

  // this will get it from the static table
  CHECK_EQ(context.getIndex(method), 3);
}

TEST_F(QCRAMContextTests, is_static) {
  TestQCRAMContext context(HPACK::kTableSize);
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

TEST_F(QCRAMContextTests, static_table) {
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

TEST_F(QCRAMContextTests, static_index) {
  TestQCRAMContext context(HPACK::kTableSize);
  HPACKHeader authority(":authority", "");
  context.getHeader(1).then([&] (QCRAMHeaderTable::DecodeResult res) {
      EXPECT_EQ(res.ref, authority);
    });

  HPACKHeader post(":method", "POST");
  context.getHeader(3).then([&] (QCRAMHeaderTable::DecodeResult res) {
      EXPECT_EQ(res.ref, post);
    });

  HPACKHeader contentLength("content-length", "");
  context.getHeader(28).then([&] (QCRAMHeaderTable::DecodeResult res) {
      EXPECT_EQ(res.ref, contentLength);
    });
}

TEST_F(QCRAMContextTests, encoder_multiple_values) {
  QCRAMEncoder encoder(true);
  vector<HPACKHeader> req;
  req.push_back(HPACKHeader("accept-encoding", "gzip"));
  req.push_back(HPACKHeader("accept-encoding", "sdch,gzip"));
  unique_ptr<IOBuf> encoded = encoder.encode(req);
  EXPECT_TRUE(encoded->length() > 0);
  EXPECT_EQ(encoder.getTable().size(), 2);
  QCRAMDecoder decoder(*this);
  decodeStreaming(decoder, encoded.get());
  EXPECT_TRUE(cb.getResult().isOk());
  // sending the same request again should lead to a smaller but non
  // empty buffer
  unique_ptr<IOBuf> encoded2 = encoder.encode(req);
  EXPECT_LT(encoded2->computeChainDataLength(),
            encoded->computeChainDataLength());
  EXPECT_GT(encoded2->computeChainDataLength(), 0);
  decodeStreaming(decoder, encoded2.get());
  EXPECT_TRUE(cb.getResult().isOk());
}

TEST_F(QCRAMContextTests, decoder_large_header) {
  // with this size basically the table will not be able to store any entry
  uint32_t size = 32;
  HPACKHeader header;
  QCRAMEncoder encoder(true, size);
  QCRAMDecoder decoder(*this, size);
  vector<HPACKHeader> headers;
  headers.push_back(HPACKHeader(":path", "verylargeheader"));
  // add a static entry
  headers.push_back(HPACKHeader(":method", "GET"));
  auto buf = encoder.encode(headers);
  decodeStreaming(decoder, buf.get());
  EXPECT_EQ(encoder.getTable().size(), 0);
  EXPECT_EQ(decoder.getTable().size(), 0);
  EXPECT_TRUE(cb.getResult().isOk());
  EXPECT_EQ(cb.getResult().ok().headers.size(), 4);
}


TEST_F(QCRAMContextTests, eviction) {
  // with this size and 256 min free, 6x43 byte headers will trigger an
  // eviction
  uint32_t size = 512;
  HPACKHeader header;
  QCRAMEncoder encoder(true, size);
  QCRAMDecoder decoder(*this, size);
  vector<HPACKHeader> headers;
  for (int i = 1; i <= 6; i++) {
    headers.emplace_back("name" + folly::to<string>(i),
                         "value" + folly::to<string>(i));
  }
  auto buf = encoder.encode(headers);
  // there are 6 entries in the table, but one is marked invalid
  EXPECT_EQ(encoder.getTable().size(), 6);
  decodeStreaming(decoder, buf.get());
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

/**
 * testing invalid memory access in the decoder; it has to always call peek()
 */
TEST_F(QCRAMContextTests, decoder_invalid_peek) {
  QCRAMEncoder encoder(true);
  QCRAMDecoder decoder(*this);
  vector<HPACKHeader> headers;
  headers.push_back(HPACKHeader("x-fb-debug", "test"));

  unique_ptr<IOBuf> encoded = encoder.encode(headers);
  unique_ptr<IOBuf> first = IOBuf::create(128);
  // set a trap for indexed header and don't call append
  first->writableData()[0] = HPACK::HeaderEncoding::INDEXED;

  first->appendChain(std::move(encoded));
  decodeStreaming(decoder, first.get());

  EXPECT_TRUE(cb.getResult().isOk());
  EXPECT_EQ(cb.getResult().ok().headers.size(), headers.size() * 2);
}

/**
 * similar with the one above, but slightly different code paths
 */
TEST_F(QCRAMContextTests, decoder_invalid_literal_peek) {
  QCRAMEncoder encoder(true);
  QCRAMDecoder decoder(*this);
  vector<HPACKHeader> headers;
  headers.push_back(HPACKHeader("x-fb-random", "bla"));
  unique_ptr<IOBuf> encoded = encoder.encode(headers);

  unique_ptr<IOBuf> first = IOBuf::create(128);
  first->writableData()[0] = 0x3F;

  first->appendChain(std::move(encoded));
  decodeStreaming(decoder, first.get());

  EXPECT_TRUE(cb.getResult().isOk());
  EXPECT_EQ(cb.getResult().ok().headers.size(), headers.size() * 2);
}

/**
 * testing various error cases in QCRAMDecoder::decodeLiterHeader()
 */
void QCRAMContextTests::checkError(const IOBuf* buf,
                                   const HeaderDecodeError err) {
  QCRAMDecoder decoder(*this);
  decodeStreaming(decoder, buf);

  EXPECT_TRUE(cb.getResult().isError());
  EXPECT_EQ(cb.getResult().error(), err);
}

TEST_F(QCRAMContextTests, decode_errors) {
  unique_ptr<IOBuf> buf = IOBuf::create(128);

  // 0. simulate an error decoding the index 0
  buf->writableData()[0] = 0x80;
  buf->append(1);  // intentionally omit the second byte
  checkError(buf.get(), HeaderDecodeError::INVALID_INDEX);

  // 1. simulate an error decoding the index for an indexed header name
  // we try to encode index 65
  buf->writableData()[0] = 0x0F;
  buf->append(1);  // intentionally omit the second byte
  checkError(buf.get(), HeaderDecodeError::BUFFER_UNDERFLOW);

  // 2. invalid index (new index in static range
  buf->writableData()[0] = 0x41;
  checkError(buf.get(), HeaderDecodeError::INVALID_INDEX);

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
// TABLE_SIZE_UPDATE is actually a QCRAM delete instruction, so this test
// doesn't do what it claims.
TEST_F(QCRAMContextTests, contextUpdate) {
  // using hpack to generate a frame with a table size update in it
  HPACKEncoder encoder(true);
  QCRAMDecoder decoder(*this);
  vector<HPACKHeader> headers;
  encoder.setHeaderTableSize(8192);
  headers.push_back(HPACKHeader("x-fb-random", "bla"));
  unique_ptr<IOBuf> encoded = encoder.encode(headers);

  unique_ptr<IOBuf> first = IOBuf::create(128);

  first->appendChain(std::move(encoded));
  decodeStreaming(decoder, first.get());

  EXPECT_TRUE(cb.getResult().isError());
  EXPECT_EQ(cb.getResult().error(), HeaderDecodeError::INVALID_TABLE_SIZE);
}
#endif
