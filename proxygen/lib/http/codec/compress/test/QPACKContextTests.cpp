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
    EXPECT_EQ(decoder.decodeEncoderStream(std::move(result.control)),
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
  // streamID only matters for cancellation
  decoder.decodeStreaming(0, std::move(result.stream), length, cb.get());
  EXPECT_EQ(cb->error, expectedError);
}

bool stringInOutput(IOBuf* stream, const std::string& expected) {
  stream->coalesce();
  return memmem(stream->data(), stream->length(),
                expected.data(), expected.length());
}

HPACK::DecodeError headerAck(QPACKDecoder& decoder, QPACKEncoder& encoder,
                             uint64_t streamId) {
  return encoder.decodeDecoderStream(decoder.encodeHeaderAck(streamId));
}

HPACK::DecodeError cancelStream(QPACKDecoder& decoder, QPACKEncoder& encoder,
                                uint64_t streamId) {
  return encoder.decodeDecoderStream(decoder.encodeCancelStream(streamId));
}
}

TEST(QPACKContextTests, StaticOnly) {
  QPACKEncoder encoder(true, 128);
  QPACKDecoder decoder(128);
  vector<HPACKHeader> req;
  req.push_back(HPACKHeader("accept-encoding", "gzip, deflate"));
  auto result = encoder.encode(req, 10, 1);
  EXPECT_EQ(result.stream->computeChainDataLength(), 3);
  EXPECT_EQ(result.stream->data()[0], 0);
  EXPECT_EQ(result.stream->data()[1], 0);
  verifyDecode(decoder, std::move(result), req);
  // nothing to ack
  EXPECT_EQ(decoder.encodeTableStateSync(), nullptr);
}

TEST(QPACKContextTests, Indexed) {
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

TEST(QPACKContextTests, NameIndexed) {
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

TEST(QPACKContextTests, NameIndexedInsert) {
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

TEST(QPACKContextTests, PostBaseNameIndexedLiteral) {
  QPACKEncoder encoder(false, 360);
  QPACKDecoder decoder(360);
  vector<HPACKHeader> req;

  encoder.setMaxVulnerable(1);
  // Fills the table with exacty minFree (48) empty
  for (auto i = 0; i < 8; i++) {
    req.push_back(HPACKHeader(folly::to<std::string>("Blarf", i), "0"));
  }
  // Too big to put in the table without evicting, perfect
  // for Post-Base Name-Indexed literal with idx=7
  req.push_back(HPACKHeader("Blarf7", "blergblergblerg"));
  auto result = encoder.encode(req, 10, 1);
  EXPECT_EQ(result.stream->computeChainDataLength(),
            2 /*prefix*/ + 8 /*pb indexed*/ + 2 /*name idx len*/ +
            1 /*val len*/ + 15 /* value */);
  verifyDecode(decoder, std::move(result), req);
}


TEST(QPACKContextTests, Unacknowledged) {
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

TEST(QPACKContextTests, TestDraining) {
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

TEST(QPACKContextTests, TestDuplicate) {
  QPACKEncoder encoder(false, 200);
  QPACKDecoder decoder(200);
  vector<HPACKHeader> req;
  // 5 inserts and one literal
  for (auto i = 0; i < 6; i++) {
    req.emplace_back(folly::to<string>('a' + i), folly::to<string>(i));
  }
  // a=0 should now be draining
  auto result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);
  EXPECT_EQ(encoder.onTableStateSync(5), HPACK::DecodeError::NONE);
  EXPECT_EQ(headerAck(decoder, encoder, 1), HPACK::DecodeError::NONE);
  req.erase(req.begin() + 1, req.end());
  result = encoder.encode(req, 0, 2);
  // Control contains one-byte duplicate instruction, stream prefix + 1
  EXPECT_EQ(result.control->computeChainDataLength(), 1);
  EXPECT_EQ(result.stream->computeChainDataLength(), 3);
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, TestTableSizeUpdate) {
  QPACKEncoder encoder(false, 100);
  QPACKDecoder decoder(100);
  vector<HPACKHeader> req;
  req.emplace_back("Blarf", "Blah");
  req.emplace_back("Blarf", "Blerg");
  auto result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);
  EXPECT_EQ(encoder.onTableStateSync(2), HPACK::DecodeError::NONE);
  EXPECT_EQ(headerAck(decoder, encoder, 1), HPACK::DecodeError::NONE);
  encoder.setHeaderTableSize(64); // This will evict the oldest header
  EXPECT_EQ(encoder.getHeadersStored(), 1);
  result = encoder.encode(req, 0, 2);
  verifyDecode(decoder, std::move(result), req);
  EXPECT_EQ(decoder.getHeadersStored(), 1);
  EXPECT_EQ(headerAck(decoder, encoder, 2), HPACK::DecodeError::NONE);

  encoder.setHeaderTableSize(100);
  result = encoder.encode(req, 0, 3);
  EXPECT_EQ(encoder.getHeadersStored(), 2);
  verifyDecode(decoder, std::move(result), req);
  EXPECT_EQ(decoder.getHeadersStored(), 2);
}


TEST(QPACKContextTests, TestAcks) {
  QPACKEncoder encoder(false, 100);
  QPACKDecoder decoder(100);
  encoder.setMaxVulnerable(1);
  EXPECT_EQ(encoder.onTableStateSync(1), HPACK::DecodeError::INVALID_ACK);
  EXPECT_EQ(headerAck(decoder, encoder, 1), HPACK::DecodeError::INVALID_ACK);

  vector<HPACKHeader> req;
  req.emplace_back("Blarf", "BlahBlahBlah");
  auto result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);
  req.clear();
  req.emplace_back("accept-encoding", "gzip, deflate");
  result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);
  req.clear();
  req.emplace_back("Blarf", "BlahBlahBlah");
  result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);

  // Blarf: Blah is unacknowledged and maxVulnerable is 1 -> literal
  result = encoder.encode(req, 0, 2);
  EXPECT_EQ(result.control, nullptr);
  EXPECT_TRUE(stringInOutput(result.stream.get(), "blarf"));
  verifyDecode(decoder, std::move(result), req);

  // Table is full and Blarf: BlahBlahBlah cannot be evicted -> literal
  req.clear();
  req.emplace_back("Foo", "BlahBlahBlahBlah!");
  result = encoder.encode(req, 0, 3);
  EXPECT_EQ(result.control, nullptr);
  EXPECT_TRUE(stringInOutput(result.stream.get(), "foo"));
  verifyDecode(decoder, std::move(result), req);
  EXPECT_EQ(headerAck(decoder, encoder, 3), HPACK::DecodeError::NONE);

  // Should remove all encoder state.  Blarf: BlahBlahBlah can now be evicted
  // and a new vulnerable reference can be made.
  EXPECT_EQ(headerAck(decoder, encoder, 2), HPACK::DecodeError::NONE);
  EXPECT_EQ(cancelStream(decoder, encoder, 1), HPACK::DecodeError::NONE);
  EXPECT_EQ(encoder.onTableStateSync(1), HPACK::DecodeError::NONE);

  result = encoder.encode(req, 0, 2);
  // Encodes an insert
  EXPECT_GT(result.control->computeChainDataLength(), 1);
  EXPECT_EQ(result.stream->computeChainDataLength(), 3);
  EXPECT_FALSE(stringInOutput(result.stream.get(), "foo"));
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, TestImplicitAcks) {
  QPACKEncoder encoder(false, 1024);
  QPACKDecoder decoder(1024);
  encoder.setMaxVulnerable(2);

  vector<HPACKHeader> req;
  req.emplace_back("Blarf", "Blah");
  auto result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req);
  req.emplace_back("Foo", "Blah");
  result = encoder.encode(req, 0, 2);
  verifyDecode(decoder, std::move(result), req);
  EXPECT_EQ(encoder.onHeaderAck(2, false), HPACK::DecodeError::NONE);
  // both headers are now acknowledged, 1 unacked header allowed
  req.clear();
  req.emplace_back("Bar", "Binky");
  result = encoder.encode(req, 0, 3);

  // No unacked headers allowed
  req.emplace_back("Blarf", "Blah");
  req.emplace_back("Foo", "Blah");
  result = encoder.encode(req, 0, 4);
  EXPECT_FALSE(stringInOutput(result.stream.get(), "Blah"));
  verifyDecode(decoder, std::move(result), req);

  // cancel
  EXPECT_EQ(encoder.onHeaderAck(2, true), HPACK::DecodeError::NONE);
  EXPECT_EQ(encoder.onHeaderAck(4, true), HPACK::DecodeError::NONE);
}

TEST(QPACKContextTests, TestDecodeQueue) {
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

TEST(QPACKContextTests, TestDecodeQueueDelete) {
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
  rawDecoder->decodeStreaming(1, std::move(result1.stream), length, rawCb1);

  // Decode #2, no control stream, queued
  auto cb2 = std::make_unique<TestStreamingCallback>();
  length = result2.stream->computeChainDataLength();
  rawDecoder->decodeStreaming(2, std::move(result2.stream), length, cb2.get());

  // Decode control stream #1, will unblock 1 and delete decoder
  EXPECT_EQ(rawDecoder->decodeEncoderStream(std::move(result1.control)),
            HPACK::DecodeError::NONE);

  // cb2 doesn't execute because the decoder was destroyed from cb1
  EXPECT_EQ(cb2->error, HPACK::DecodeError::NONE);
  EXPECT_EQ(cb2->headers.size(), 0);
}

TEST(QPACKContextTests, TestDecodeMaxUncompressed) {
  QPACKEncoder encoder(false, 64);
  QPACKDecoder decoder(64);
  decoder.setMaxUncompressed(5);

  vector<HPACKHeader> req;
  req.emplace_back("Blarf", "Blah");
  auto result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req,
               HPACK::DecodeError::HEADERS_TOO_LARGE);
}

TEST(QPACKContextTests, TestDecoderStreamChunked) {
  QPACKEncoder encoder(false, 5000);
  QPACKDecoder decoder(5000);

  vector<HPACKHeader> req;
  for (auto i = 0; i < 128; i++) {
    req.emplace_back("a", folly::to<string>(i));
  }
  auto result = encoder.encode(req, 0, 1);
  EXPECT_EQ(decoder.decodeEncoderStream(std::move(result.control)),
            HPACK::DecodeError::NONE);
  auto ack = decoder.encodeTableStateSync();
  EXPECT_EQ(ack->computeChainDataLength(), 2);
  auto ackPart = ack->clone();
  ackPart->trimEnd(1);
  ack->trimStart(1);
  EXPECT_EQ(encoder.decodeDecoderStream(std::move(ackPart)),
            HPACK::DecodeError::NONE);
  EXPECT_EQ(encoder.decodeDecoderStream(std::move(ack)),
            HPACK::DecodeError::NONE);
  EXPECT_FALSE(encoder.getTable().isVulnerable(128));
  EXPECT_TRUE(encoder.getTable().isVulnerable(129));
}

TEST(QPACKContextTests, TestDecodePartialControl) {
  QPACKEncoder encoder(false, 1000);
  QPACKDecoder decoder(1000);

  vector<HPACKHeader> req;
  req.emplace_back("abcdeabcdeabcdeabcdeabcdeabcdeabcde",
                   "vwxyzvwxyzvwxyzvwxyzvwxyzvwxyzvwxyz");
  auto result = encoder.encode(req, 0, 1);
  folly::io::Cursor c(result.control.get());
  while (!c.isAtEnd()) {
    std::unique_ptr<folly::IOBuf> buf;
    c.clone(buf, 1);
    EXPECT_EQ(decoder.decodeEncoderStream(std::move(buf)),
              HPACK::DecodeError::NONE);
  }
  EXPECT_EQ(decoder.getHeadersStored(), 1);
  EXPECT_EQ(decoder.getHeader(false, 1, 1, false), req[0]);
}

void checkQError(QPACKDecoder& decoder, std::unique_ptr<IOBuf> buf,
                 const HPACK::DecodeError err) {
  auto cb = std::make_unique<TestStreamingCallback>();
  auto len = buf->computeChainDataLength();
  // streamID only matters for cancellation
  decoder.decodeStreaming(0, std::move(buf), len, cb.get());
  EXPECT_EQ(cb->error, err);
}

TEST(QPACKContextTests, DecodeErrors) {
  QPACKDecoder decoder(128);
  unique_ptr<IOBuf> buf = IOBuf::create(128);

  // Largest ref invalid
  buf->writableData()[0] = 0xFF;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // Base delta missing
  buf->writableData()[0] = 0x01;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // Base delta invalid
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
  buf->writableData()[2] = 0x4F;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // Literal invalid name index
  buf->writableData()[2] = 0x41;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::INVALID_INDEX);

  // Literal bad name length
  buf->writableData()[2] = 0x27;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  // Literal invalid value length
  buf->writableData()[2] = 0x51;
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
  EXPECT_EQ(decoder.decodeEncoderStream(buf->clone()),
            HPACK::DecodeError::NONE);

  // Control decode error
  QPACKDecoder decoder2(64);
  buf->writableData()[0] = 0x01; // duplicate dynamic index 1
  buf->trimEnd(2);
  EXPECT_EQ(decoder2.decodeEncoderStream(buf->clone()),
            HPACK::DecodeError::INVALID_INDEX);

  QPACKEncoder encoder(true, 128);
  buf->writableData()[0] = 0xFF;
  buf->writableData()[1] = 0x80;
  buf->writableData()[2] = 0xFF;
  buf->writableData()[3] = 0xFF;
  buf->writableData()[4] = 0xFF;
  buf->writableData()[5] = 0xFF;
  buf->writableData()[6] = 0xFF;
  buf->writableData()[7] = 0xFF;
  buf->writableData()[8] = 0xFF;
  buf->writableData()[9] = 0xFF;
  buf->writableData()[10] = 0xFF;
  buf->writableData()[11] = 0x01;
  buf->append(11);
  // Bad header ack
  EXPECT_EQ(encoder.decodeDecoderStream(buf->clone()),
            HPACK::DecodeError::INTEGER_OVERFLOW);

  // Bad cancel
  buf->writableData()[0] = 0x7F;
  EXPECT_EQ(encoder.decodeDecoderStream(buf->clone()),
            HPACK::DecodeError::INTEGER_OVERFLOW);

  // Bad table state sync
  buf->writableData()[0] = 0x3F;
  EXPECT_EQ(encoder.decodeDecoderStream(buf->clone()),
            HPACK::DecodeError::INTEGER_OVERFLOW);
}

TEST(QPACKContextTests, TestEvictedNameReference) {
  QPACKEncoder encoder(false, 109);
  QPACKDecoder decoder(109);
  encoder.setMaxVulnerable(0);
  vector<HPACKHeader> req;
  req.push_back(HPACKHeader("x-accept-encoding", "foobarfoobar"));
  auto result = encoder.encode(req, 0, 1);
  decoder.decodeEncoderStream(std::move(result.control));
  decoder.decodeStreaming(1, result.stream->clone(),
                          result.stream->computeChainDataLength(), nullptr);
  encoder.onTableStateSync(1);
  req.clear();
  req.push_back(HPACKHeader("x-accept-encoding", "barfoobarfoo"));
  result = encoder.encode(req, 0, 2);
  EXPECT_TRUE(stringInOutput(result.stream.get(), "x-accept-encoding"));
  TestStreamingCallback cb;
  decoder.decodeEncoderStream(std::move(result.control));
  decoder.decodeStreaming(2, result.stream->clone(),
                          result.stream->computeChainDataLength(), &cb);
  EXPECT_FALSE(cb.hasError());
}
