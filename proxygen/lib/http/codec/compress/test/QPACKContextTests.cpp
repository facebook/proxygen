/*
 *  Copyright (c) 2018-present, Facebook, Inc.
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
#include <proxygen/lib/http/codec/compress/QPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/QPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <proxygen/lib/http/codec/compress/test/TestStreamingCallback.h>

using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;

namespace {
void verifyDecode(QPACKDecoder& decoder, QPACKEncoder::EncodeResult result,
                  const std::vector<HPACKHeader>& expectedHeaders,
                  HPACK::DecodeError expectedError = HPACK::DecodeError::NONE) {
  auto cb = std::make_shared<TestStreamingCallback>();
  if (result.control) {
    folly::io::Cursor control(result.control.get());
    EXPECT_EQ(decoder.decodeControl(control, control.totalLength()),
              HPACK::DecodeError::NONE);
  }
  auto length = result.stream->computeChainDataLength();
  if (expectedError == HPACK::DecodeError::NONE) {
    cb->headersCompleteCb =
      [&expectedHeaders, cb] () mutable {
      std::vector<HPACKHeader> test;
      for (size_t i = 0; i < cb->headers.size(); i += 2) {
        test.emplace_back(cb->headers[i].str, cb->headers[i + 1].str);
      }
      EXPECT_EQ(cb->error, HPACK::DecodeError::NONE);
      EXPECT_EQ(test, expectedHeaders);
      cb.reset();
    };
  }
  decoder.decodeStreaming(std::move(result.stream), length, cb.get());
  EXPECT_EQ(cb->error, expectedError);
}

bool stringInOutput(IOBuf* stream, const std::string& expected) {
  stream->coalesce();
  return memmem(stream->data(), stream->length(),
                expected.data(), expected.length());
}
}

TEST(QPACKContextTests, static_only) {
  QPACKEncoder encoder(true, 128);
  QPACKDecoder decoder(128);
  vector<HPACKHeader> req;
  req.push_back(HPACKHeader("accept-encoding", "gzip, deflate"));
  auto result = encoder.encode(req, 10, 1);
  EXPECT_EQ(result.stream->computeChainDataLength(), 3);
  EXPECT_EQ(result.stream->data()[0], 0);
  EXPECT_EQ(result.stream->data()[1], 0);
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, indexed) {
  QPACKEncoder encoder(true, 128);
  QPACKDecoder decoder(128);
  vector<HPACKHeader> req;
  // Encodes "Post Base"
  req.push_back(HPACKHeader("Blarf", "Blah"));
  auto result = encoder.encode(req, 10, 1);
  verifyDecode(decoder, std::move(result), req);
  // Encodes "Normal"
  result = encoder.encode(req, 10, 2);
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, name_indexed) {
  QPACKEncoder encoder(true, 64);
  QPACKDecoder decoder(64);
  vector<HPACKHeader> req;

  // Encodes a "Post Base" name index since the table is full
  req.push_back(HPACKHeader("Blarf", "Blah"));
  req.push_back(HPACKHeader("Blarf", "Blerg"));
  auto result = encoder.encode(req, 10, 1);
  verifyDecode(decoder, std::move(result), req);
  // Encodes "Normal" name index
  result = encoder.encode(req, 10, 2);
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, name_indexed_insert) {
  QPACKEncoder encoder(false, 128);
  QPACKDecoder decoder(128);
  vector<HPACKHeader> req;

  req.push_back(HPACKHeader("Blarf", "Blah"));
  auto result = encoder.encode(req, 10, 1);
  verifyDecode(decoder, std::move(result), req);

  // Encodes an insert using a dynamic name reference
  req.push_back(HPACKHeader("Blarf", "Blerg"));
  result = encoder.encode(req, 10, 2);
  EXPECT_FALSE(stringInOutput(result.control.get(), "blarf"));
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, unacknowledged) {
  QPACKEncoder encoder(true, 128);
  QPACKDecoder decoder(128);
  // Disallow unack'd headers
  encoder.setMaxVulnerable(0);
  vector<HPACKHeader> req;
  req.push_back(HPACKHeader("Blarf", "Blah"));
  auto result = encoder.encode(req, 10, 1);

  // Stream will encode a literal: prefix(2) + <more than 1>
  EXPECT_GT(result.stream->computeChainDataLength(), 3);
  verifyDecode(decoder, std::move(result), req);

  req.push_back(HPACKHeader("Blarf", "Blerg"));
  result = encoder.encode(req, 10, 2);
  EXPECT_GT(result.stream->computeChainDataLength(), 4);
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, test_draining) {
  QPACKEncoder encoder(false, 128);
  vector<HPACKHeader> req;
  req.push_back(HPACKHeader("accept-encoding", "gzip,deflate"));
  auto result = encoder.encode(req, 0, 1);

  // This will result in the first header being drained in the middle
  // of encoding the new control channel, and force a literal.
  req.clear();
  req.push_back(HPACKHeader("accept-encoding", "sdch,gzip"));
  req.push_back(HPACKHeader("accept-encoding", "gzip,deflate"));
  result = encoder.encode(req, 0, 2);
  EXPECT_GT(result.stream->computeChainDataLength(), 4);
  EXPECT_TRUE(stringInOutput(result.stream.get(), "gzip,deflate"));
}

TEST(QPACKContextTests, test_duplicate) {
  QPACKEncoder encoder(false, 200);
  QPACKDecoder decoder(200);
  vector<HPACKHeader> req;
  for (auto i = 0; i < 6; i++) {
    req.emplace_back(folly::to<string>('a' + i), folly::to<string>(i));
  }
  // a=0 should now be draining
  auto result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);
  encoder.onControlHeaderAck();
  encoder.onHeaderAck(1, false);
  req.erase(req.begin() + 1, req.end());
  result = encoder.encode(req, 0, 2);
  // Control contains one-byte duplicate instruction, stream prefix + 1
  EXPECT_EQ(result.control->computeChainDataLength(), 1);
  EXPECT_EQ(result.stream->computeChainDataLength(), 3);
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, test_table_size_update) {
  QPACKEncoder encoder(false, 100);
  QPACKDecoder decoder(100);
  vector<HPACKHeader> req;
  req.emplace_back("Blarf", "Blah");
  req.emplace_back("Blarf", "Blerg");
  auto result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);
  encoder.onControlHeaderAck();
  encoder.onHeaderAck(1, false);
  encoder.setHeaderTableSize(64); // This will evict the oldest header
  EXPECT_EQ(encoder.getHeadersStored(), 1);
  result = encoder.encode(req, 0, 2);
  verifyDecode(decoder, std::move(result), req);
  EXPECT_EQ(decoder.getHeadersStored(), 1);
  encoder.onControlHeaderAck();
  encoder.onHeaderAck(1, false);

  encoder.setHeaderTableSize(100);
  result = encoder.encode(req, 0, 1);
  EXPECT_EQ(encoder.getHeadersStored(), 2);
  verifyDecode(decoder, std::move(result), req);
  EXPECT_EQ(decoder.getHeadersStored(), 2);
}


TEST(QPACKContextTests, test_acks) {
  QPACKEncoder encoder(false, 64);
  QPACKDecoder decoder(64);
  encoder.setMaxVulnerable(1);
  EXPECT_EQ(encoder.onControlHeaderAck(), HPACK::DecodeError::INVALID_ACK);
  EXPECT_EQ(encoder.onHeaderAck(1, false), HPACK::DecodeError::INVALID_ACK);

  vector<HPACKHeader> req;
  req.emplace_back("Blarf", "Blah");
  auto result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);
  EXPECT_EQ(encoder.onHeaderAck(1, false), HPACK::DecodeError::NONE);
  req.clear();
  req.emplace_back("accept-encoding", "gzip, deflate");
  result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);
  req.clear();
  req.emplace_back("Blarf", "Blah");
  result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);

  // Blarf: Blah is unacknowledged and maxVulnerable is 1 -> literal
  result = encoder.encode(req, 0, 2);
  EXPECT_EQ(result.control, nullptr);
  EXPECT_TRUE(stringInOutput(result.stream.get(), "blarf"));
  verifyDecode(decoder, std::move(result), req);

  // Table is full and Blarf: Blah cannot be evicted -> literal
  req.clear();
  req.emplace_back("Foo", "Blah");
  result = encoder.encode(req, 0, 3);
  EXPECT_EQ(result.control, nullptr);
  EXPECT_TRUE(stringInOutput(result.stream.get(), "foo"));
  verifyDecode(decoder, std::move(result), req);
  encoder.onHeaderAck(3, false);

  // Should remove all encoder state.  Blarf: Blah can now be evicted and
  // a new vulnerable reference can be made.
  encoder.onHeaderAck(2, false);
  encoder.onHeaderAck(1, true);

  result = encoder.encode(req, 0, 2);
  // Encodes an insert
  EXPECT_GT(result.control->computeChainDataLength(), 1);
  EXPECT_EQ(result.stream->computeChainDataLength(), 3);
  EXPECT_FALSE(stringInOutput(result.stream.get(), "foo"));
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, test_decode_queue) {
  QPACKEncoder encoder(false, 64);
  QPACKDecoder decoder(64);

  vector<HPACKHeader> req1;
  req1.emplace_back("Blarf", "Blah");
  auto result1 = encoder.encode(req1, 0, 1);

  vector<HPACKHeader> req2;
  req2.emplace_back("Blarf", "Blerg");
  auto result2 = encoder.encode(req2, 0, 2);
  verifyDecode(decoder, std::move(result2), req2);
  verifyDecode(decoder, std::move(result1), req1);
}

TEST(QPACKContextTests, test_decode_queue_delete) {
  // This test deletes the decoder from a callback while there are items in
  // the queue
  QPACKEncoder encoder(true, 100);
  auto decoder = std::make_unique<QPACKDecoder>(100);

  vector<HPACKHeader> req1;
  req1.emplace_back("Blarf", "Blah");
  auto result1 = encoder.encode(req1, 0, 1);

  vector<HPACKHeader> req2;
  req2.emplace_back("Blarf", "Blerg");
  auto result2 = encoder.encode(req2, 0, 2);


  // Decode #1, no control stream, queued
  auto cb1 = std::make_unique<TestStreamingCallback>();
  auto rawCb1 = cb1.get();
  auto rawDecoder = decoder.get();
  cb1->headersCompleteCb = [decoder=std::move(decoder)] () mutable {
    // Delete decoder from callback
    decoder.reset();
  };
  auto length = result1.stream->computeChainDataLength();
  rawDecoder->decodeStreaming(std::move(result1.stream), length, rawCb1);

  // Decode #2, no control stream, queued
  auto cb2 = std::make_unique<TestStreamingCallback>();
  length = result2.stream->computeChainDataLength();
  rawDecoder->decodeStreaming(std::move(result2.stream), length, cb2.get());

  // Decode control stream #1, will unblock 1 and delete decoder
  folly::io::Cursor control(result1.control.get());
  EXPECT_EQ(rawDecoder->decodeControl(control, control.totalLength()),
            HPACK::DecodeError::NONE);

  // cb2 doesn't execute because the decoder was destroyed from cb1
  EXPECT_EQ(cb2->error, HPACK::DecodeError::NONE);
  EXPECT_EQ(cb2->headers.size(), 0);
}

TEST(QPACKContextTests, test_decode_max_uncompressed) {
  QPACKEncoder encoder(false, 64);
  QPACKDecoder decoder(64);
  decoder.setMaxUncompressed(5);

  vector<HPACKHeader> req;
  req.emplace_back("Blarf", "Blah");
  auto result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req,
               HPACK::DecodeError::HEADERS_TOO_LARGE);
}

void checkQError(QPACKDecoder& decoder, std::unique_ptr<IOBuf> buf,
                 const HPACK::DecodeError err) {
  auto cb = std::make_unique<TestStreamingCallback>();
  auto len = buf->computeChainDataLength();
  decoder.decodeStreaming(std::move(buf), len, cb.get());
  EXPECT_EQ(cb->error, err);
}

TEST(QPACKContextTests, decode_errors) {
  QPACKDecoder decoder(128);
  unique_ptr<IOBuf> buf = IOBuf::create(128);

  // Largest ref invalid
  buf->writableData()[0] = 0xFF;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // Base delta invalid
  buf->writableData()[0] = 0x01;
  buf->writableData()[1] = 0xFF;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // Base delta too negative
  buf->writableData()[0] = 0x01;
  buf->writableData()[1] = 0x82;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::INVALID_INDEX);

  // Exceeds blocking max
  decoder.setMaxBlocking(0);
  buf->writableData()[0] = 0x01;
  buf->writableData()[1] = 0x00;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::TOO_MANY_BLOCKING);

  // valid prefix
  buf->writableData()[0] = 0x00;
  buf->writableData()[1] = 0x00;

  // Literal bad name index
  buf->writableData()[2] = 0x0F;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // Literal invalid name index
  buf->writableData()[2] = 0x01;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::INVALID_INDEX);

  // Literal bad name length
  buf->writableData()[2] = 0x63;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // Literal invalid value length
  buf->writableData()[2] = 0x11;
  buf->writableData()[3] = 0xFF;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  buf->trimEnd(1);
  // Bad Index
  buf->writableData()[2] = 0xBF;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // Zero static index
  buf->writableData()[2] = 0xC0;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::INVALID_INDEX);

  // Invalid static index
  buf->writableData()[2] = 0xFE;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::INVALID_INDEX);

  // No error after previous error
  buf->writableData()[0] = 0xC1;
  buf->writableData()[1] = 0x01;
  buf->writableData()[2] = 0x41;
  folly::io::Cursor c(buf.get());
  EXPECT_EQ(decoder.decodeControl(c, c.totalLength()),
            HPACK::DecodeError::NONE);

  // Control decode error
  QPACKDecoder decoder2(64);
  buf->writableData()[0] = 0xFF;
  buf->trimEnd(2);
  c.reset(buf.get());
  EXPECT_EQ(decoder2.decodeControl(c, c.totalLength()),
            HPACK::DecodeError::BUFFER_UNDERFLOW);

}
