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
#include <folly/Format.h>
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
std::shared_ptr<bool>
verifyDecode(QPACKDecoder& decoder, QPACKEncoder::EncodeResult result,
             const std::vector<HPACKHeader>& expectedHeaders,
             HPACK::DecodeError expectedError = HPACK::DecodeError::NONE) {
  auto cb = std::make_shared<TestStreamingCallback>();
  auto done = std::make_shared<bool>(false);
  if (result.control) {
    EXPECT_EQ(decoder.decodeEncoderStream(std::move(result.control)),
              HPACK::DecodeError::NONE);
  }
  auto length = result.stream->computeChainDataLength();
  if (expectedError == HPACK::DecodeError::NONE) {
    cb->headersCompleteCb =
      [&expectedHeaders, cb, done] () mutable {
      std::vector<HPACKHeader> test;
      for (size_t i = 0; i < cb->headers.size(); i += 2) {
        test.emplace_back(cb->headers[i].str, cb->headers[i + 1].str);
      }
      EXPECT_EQ(cb->error, HPACK::DecodeError::NONE);
      EXPECT_EQ(test, expectedHeaders);
      *done = true;
      cb.reset();
    };
  }
  // streamID only matters for cancellation
  decoder.decodeStreaming(0, std::move(result.stream), length, cb.get());
  EXPECT_EQ(cb->error, expectedError);
  if (expectedError != HPACK::DecodeError::NONE) {
    *done = true;
  }
  return done;
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

std::string toFixedLengthString(uint32_t i) {
  CHECK_LT(i, 1000);
  return folly::format("{:3}", i).str();
}
}

TEST(QPACKContextTests, StaticOnly) {
  QPACKEncoder encoder(true, 128);
  QPACKDecoder decoder(128);
  vector<HPACKHeader> req;
  // testing static indexes on request streams (6 bits)
  req.emplace_back(":authority", ""); // qpack idx=0
  req.emplace_back("x-xss-protection", "1; mode=block"); // idx=62
  req.emplace_back(":status", "100"); // idx=63
  req.emplace_back("x-frame-options", "sameorigin"); // idx=last
  auto result = encoder.encode(req, 10, 1);
  EXPECT_EQ(result.control, nullptr);
  // prefix(2) + instr(1) + instr(1) + instr(2) + instr(2)
  EXPECT_EQ(result.stream->computeChainDataLength(), 8);
  EXPECT_EQ(result.stream->data()[0], 0);
  EXPECT_EQ(result.stream->data()[1], 0);
  verifyDecode(decoder, std::move(result), req);
  // nothing to ack
  EXPECT_EQ(decoder.encodeTableStateSync(), nullptr);
}

TEST(QPACKContextTests, StaticNameIndex) {
  QPACKEncoder encoder(false, 210);
  QPACKDecoder decoder(210);
  vector<HPACKHeader> req;

  // testing static name indexes on the control stream (6 bits)
  req.emplace_back(":authority", "foo.com"); // qpack idx=0
  req.emplace_back("x-xss-protection", "maximum"); // idx=62
  // :status at index 63 won't be used by our encoder, it will prefer idx=24
  req.emplace_back("accept-language", "c++"); // idx=72
  req.emplace_back("x-frame-options", "zzz"); // idx=last
  auto result = encoder.encode(req, 10, 1);
  // instr(1) + len(1) + foo.com(7) + instr(1) + len(1) + maximum(7) +
  // instr(2) + len(1) + c++(3) + instr(2) + len(1) + zzz(3) = 30
  EXPECT_EQ(result.control->computeChainDataLength(), 30);
  EXPECT_EQ(result.stream->computeChainDataLength(), 6);
  verifyDecode(decoder, std::move(result), req);

  req.clear();
  // testing static name indexes in literals (4 bits)
  encoder.onHeaderAck(1, false);
  encoder.setHeaderTableSize(0);
  req.emplace_back("set-cookie", "abc"); // idx=14
  req.emplace_back(":method", "DUDE"); // idx=15
  result = encoder.encode(req, 10, 1);
  // prefix(2) + instr(1) + len(1) + abc(3) + instr(2) + len(1) + DUDE(4) = 14
  EXPECT_EQ(result.stream->computeChainDataLength(), 14);
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, Indexed) {
  QPACKEncoder encoder(true, 128);
  QPACKDecoder decoder(128);
  vector<HPACKHeader> req;
  // Encodes "Post Base"
  req.emplace_back("Blarf", "Blah");
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
  req.emplace_back("Blarf", "Blah");
  req.emplace_back("Blarf", "Blerg");
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

  req.emplace_back("Blarf", "Blah");
  auto result = encoder.encode(req, 10, 1);
  verifyDecode(decoder, std::move(result), req);

  // Encodes an insert using a dynamic name reference
  req.emplace_back("Blarf", "Blerg");
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
    req.emplace_back(folly::to<std::string>("Blarf", i), "0");
  }
  // Too big to put in the table without evicting, perfect
  // for Post-Base Name-Indexed literal with idx=7
  req.emplace_back("Blarf7", "blergblergblerg");
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
  req.emplace_back("Blarf", "Blah");
  auto result = encoder.encode(req, 10, 1);

  // Stream will encode a literal: prefix(2) + <more than 1>
  EXPECT_GT(result.stream->computeChainDataLength(), 3);
  verifyDecode(decoder, std::move(result), req);

  req.emplace_back("Blarf", "Blerg");
  result = encoder.encode(req, 10, 2);
  EXPECT_GT(result.stream->computeChainDataLength(), 4);
  verifyDecode(decoder, std::move(result), req);
}

TEST(QPACKContextTests, TestDraining) {
  QPACKEncoder encoder(false, 128);
  vector<HPACKHeader> req;
  req.emplace_back("accept-encoding", "gzip,deflate");
  auto result = encoder.encode(req, 0, 1);

  // This will result in the first header being drained in the middle
  // of encoding the new control channel, and force a literal.
  req.clear();
  req.emplace_back("accept-encoding", "sdch,gzip");
  req.emplace_back("accept-encoding", "gzip,deflate");
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
  QPACKDecoder decoder(200);
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

TEST(QPACKContextTests, TestTableSizeUpdateMax) {
  // Encoder has table size 200 but decoder has 100.
  // Encoder never sends a TSU, and overflows the table.
  // Decoder fails
  QPACKEncoder encoder(false, 200);
  QPACKDecoder decoder(200);
  vector<HPACKHeader> req;
  req.emplace_back("Blarf", "Blah");
  req.emplace_back("Blarf", "Blerg");
  req.emplace_back("Blarf", "Blingo");
  decoder.setHeaderTableMaxSize(100); // lower limit, should also shrink table
  auto result = encoder.encode(req, 0, 1);
  verifyDecode(decoder, std::move(result), req,
               HPACK::DecodeError::INVALID_INDEX);
  EXPECT_EQ(decoder.getHeadersStored(), 2);
}

TEST(QPACKContextTests, TestEncoderFlowControl) {
  QPACKEncoder encoder(false, 170);
  QPACKDecoder decoder(170);
  vector<HPACKHeader> req;
  req.emplace_back("Blarf", "Blah");
  req.emplace_back("Blarf", "Blerg");
  req.emplace_back("Blarf", "Blingo");
  auto result = encoder.encode(req, 0, 1, 0);
  EXPECT_EQ(result.control, nullptr);
  verifyDecode(decoder, std::move(result), req,
               HPACK::DecodeError::NONE);
  EXPECT_EQ(decoder.getHeadersStored(), 0);

  // There is enough room for the first header only
  result = encoder.encode(req, 0, 1, 11);
  EXPECT_EQ(result.control->computeChainDataLength(), 11);
  EXPECT_FALSE(stringInOutput(result.stream.get(), "Blah"));
  EXPECT_TRUE(stringInOutput(result.stream.get(), "Blerg"));
  EXPECT_TRUE(stringInOutput(result.stream.get(), "Blingo"));
  verifyDecode(decoder, std::move(result), req,
               HPACK::DecodeError::NONE);
  EXPECT_EQ(decoder.getHeadersStored(), 1);

  // Blarf is name indexed, Blah is indexed, Blerg fits, Blingo is encoded but
  // doesn't get used because it only half-fits
  result = encoder.encode(req, 0, 1, 10);
  EXPECT_EQ(result.control->computeChainDataLength(), 15);
  EXPECT_FALSE(stringInOutput(result.stream.get(), "Blah"));
  EXPECT_FALSE(stringInOutput(result.stream.get(), "Blerg"));
  EXPECT_TRUE(stringInOutput(result.control.get(), "Blingo"));
  EXPECT_TRUE(stringInOutput(result.stream.get(), "Blingo"));
  auto controlTail = result.control->clone();
  controlTail->trimStart(10);
  result.control->trimEnd(5);
  verifyDecode(decoder, std::move(result), req,
               HPACK::DecodeError::NONE);
  EXPECT_EQ(decoder.getHeadersStored(), 2);
  EXPECT_EQ(decoder.decodeEncoderStream(std::move(controlTail)),
            HPACK::DecodeError::NONE);
  EXPECT_EQ(decoder.getHeadersStored(), 3);

  // Blah is now drained, so the next encode should produce a duplicate we
  // can't use
  req.erase(req.begin() + 1, req.end());
  result = encoder.encode(req, 0, 1, 0);
  EXPECT_EQ(result.control->computeChainDataLength(), 1);
  EXPECT_TRUE(stringInOutput(result.stream.get(), "Blah"));
  verifyDecode(decoder, std::move(result), req,
               HPACK::DecodeError::NONE);
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
  // ack is invalid because it's a pure literal
  EXPECT_EQ(headerAck(decoder, encoder, 3), HPACK::DecodeError::INVALID_ACK);

  // Should remove all encoder state.  Blarf: BlahBlahBlah can now be evicted
  // and a new vulnerable reference can be made.
  // stream 2 block was pure literals
  EXPECT_EQ(headerAck(decoder, encoder, 2), HPACK::DecodeError::INVALID_ACK);
  EXPECT_EQ(cancelStream(decoder, encoder, 1), HPACK::DecodeError::NONE);
  EXPECT_EQ(encoder.onTableStateSync(1), HPACK::DecodeError::NONE);

  result = encoder.encode(req, 0, 2);
  // Encodes an insert
  EXPECT_GT(result.control->computeChainDataLength(), 1);
  EXPECT_EQ(result.stream->computeChainDataLength(), 3);
  EXPECT_FALSE(stringInOutput(result.stream.get(), "foo"));
  verifyDecode(decoder, std::move(result), req);

  EXPECT_EQ(encoder.onTableStateSync(0), HPACK::DecodeError::INVALID_ACK);
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

TEST(QPACKContextTests, TestDecodeQueueResetSelf) {
  // This test calls cancelStream from inside the callback from drainQueue
  QPACKEncoder encoder(true, 100);
  QPACKDecoder decoder(100);

  vector<HPACKHeader> req1;
  req1.emplace_back("Blarf", "Blah");
  auto result1 = encoder.encode(req1, 0, 1);

  // Decode #1, no control stream, queued
  TestStreamingCallback cb1;
  cb1.headersCompleteCb = [&] {
    decoder.encodeCancelStream(1);
  };
  auto length = result1.stream->computeChainDataLength();
  decoder.decodeStreaming(1, std::move(result1.stream), length, &cb1);

  // Decode control stream #1, will unblock 1 and reset it
  EXPECT_EQ(decoder.decodeEncoderStream(std::move(result1.control)),
            HPACK::DecodeError::NONE);
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

TEST(QPACKContextTests, WrapLRBehind) {
  // This tests how LR wraps when the encoder and decoder have the same state
  uint32_t tableSize = 1024;
  uint32_t maxEntries = tableSize / 32;
  uint32_t realMaxEntries = tableSize / (32 + sizeof("999"));

  QPACKEncoder encoder(true, tableSize);
  QPACKDecoder decoder(tableSize);
  encoder.setMinFreeForTesting(0);
  for (uint32_t decoderBase = 0; decoderBase < maxEntries * 3; decoderBase++) {
    if (decoderBase > 0) {
      // add one more header to decoder
      vector<HPACKHeader> req;
      VLOG(5) << "priming decoder with h=" << decoderBase
              << " decoderBase=" << decoderBase;
      req.emplace_back(toFixedLengthString(decoderBase), "");
      auto result = encoder.encode(req, 10, 1);
      EXPECT_NE(result.control, nullptr)
        << "Every encode should produce an insert";
      EXPECT_TRUE(*verifyDecode(decoder, std::move(result), req));
      EXPECT_EQ(encoder.decodeDecoderStream(decoder.encodeHeaderAck(1)),
                HPACK::DecodeError::NONE);
    }
    for (auto largestRef =
           std::max<int64_t>(0, int64_t(decoderBase) - realMaxEntries + 1);
         largestRef <= decoderBase; largestRef++) {
      VLOG(5) << "WrapLR test decoderBase=" << decoderBase
              << " largestRef=" << largestRef;

      // Now send encode a request for the given largest reference.
      vector<HPACKHeader> req;
      if (largestRef > 0) {
        req.emplace_back(toFixedLengthString(largestRef), "");
      } else {
        req.emplace_back(":scheme", "https");
      }
      auto result = encoder.encode(req, 10, 2);
      EXPECT_EQ(result.control, nullptr); // no inserts
      CHECK_EQ(result.stream->computeChainDataLength(), 3); // prefix + 1
      // the decoder should be able to immediately decode it
      EXPECT_TRUE(*verifyDecode(decoder, std::move(result), req));
      encoder.decodeDecoderStream(decoder.encodeHeaderAck(2));
    }
  }
}

TEST(QPACKContextTests, WrapLRAhead) {
  // This tests how LR wraps when the encoder is up to a full table ahead of the
  // decoder.  tableSize is set such that realMaxEntries=64, which prevents
  // LR from being too far from base index as to expand the prefix.
  uint32_t tableSize = 4064;
  uint32_t maxEntries = tableSize / 32;
  uint32_t realMaxEntries = tableSize / (32 + sizeof("999"));

  // With QPACK-02, this would have produced an encoded stream buffer of 4
  // bytes.  Each loop of decoderBase is expensive, so start it at maxEntries,
  // and only run it until it actually would have made a difference in
  // the encoded size of largest reference.
  CHECK_LE(realMaxEntries, 256);
  for (uint32_t decoderBase = maxEntries;
       decoderBase < (256 - realMaxEntries);
       decoderBase++) {
    QPACKEncoder encoder(true, tableSize);
    QPACKDecoder decoder(tableSize);
    encoder.setMaxVulnerable(realMaxEntries);
    decoder.setMaxBlocking(realMaxEntries);
    encoder.setMinFreeForTesting(0);
    for (uint32_t i = 1; i <= decoderBase; i++) {
      vector<HPACKHeader> req;
      // populate the encoder and decode table to decoderBase.
      VLOG(5) << "priming decoder with h=" << i
              << " decoderBase=" << decoderBase;
      req.emplace_back(toFixedLengthString(i), "");
      auto result = encoder.encode(req, 10, 1);
      EXPECT_NE(result.control, nullptr)
        << "Every encode should produce an insert";
      EXPECT_TRUE(*verifyDecode(decoder, std::move(result), req));
      EXPECT_EQ(encoder.decodeDecoderStream(decoder.encodeHeaderAck(1)),
                HPACK::DecodeError::NONE);
    }
    folly::IOBufQueue controlQueue{folly::IOBufQueue::cacheChainLength()};
    std::list<std::shared_ptr<bool>> allDone;
    vector<vector<HPACKHeader>> reqs;
    reqs.reserve(2 * realMaxEntries);
    // encode realMaxEntries requests past decoderBase, and queue the decodes
    // but don't process the inserts
    for (auto largestRef = decoderBase + 1;
         largestRef <= decoderBase + realMaxEntries; largestRef++) {
      VLOG(5) << "WrapLR test decoderBase=" << decoderBase
              << " largestRef=" << largestRef;
      reqs.emplace_back();
      auto& req = reqs.back();
      req.emplace_back(toFixedLengthString(largestRef), "");
      auto result = encoder.encode(req, 10, largestRef);
      EXPECT_NE(result.control, nullptr)
        << "Every encode should produce an insert";
      controlQueue.append(std::move(result.control));
      CHECK_EQ(result.stream->computeChainDataLength(), 3); // prefix + 1
      // the decoder has to block because the control stream is pending.
      // This verifies the whole batch of encodes against the same decoderBase
      allDone.emplace_back(verifyDecode(decoder, std::move(result), req));
    }
    // control block should unblock all requests
    decoder.decodeEncoderStream(controlQueue.move());
    for (const auto& done: allDone) {
      EXPECT_TRUE(*done);
    }
  }
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

  VLOG(10) << "Largest ref invalid";
  buf->writableData()[0] = 0xFF;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  VLOG(10) << "Base delta missing";
  buf->writableData()[0] = 0x01;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  VLOG(10) << "Base delta invalid";
  buf->writableData()[1] = 0xFF;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  VLOG(10) << "Base delta too negative";
  buf->writableData()[0] = 0x01;
  buf->writableData()[1] = 0x82;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::INVALID_INDEX);

  VLOG(10) << "Exceeds blocking max";
  decoder.setMaxBlocking(0);
  buf->writableData()[0] = 0x02;
  buf->writableData()[1] = 0x00;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::TOO_MANY_BLOCKING);

  // valid prefix
  buf->writableData()[0] = 0x00;
  buf->writableData()[1] = 0x00;

  VLOG(10) << "Literal bad name index";
  buf->writableData()[2] = 0x4F;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  VLOG(10) << "Literal index name index";
  buf->writableData()[2] = 0x41;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::INVALID_INDEX);

  VLOG(10) << "Literal bad name length";
  buf->writableData()[2] = 0x27;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  VLOG(10) << "Literal invalid value length";
  buf->writableData()[2] = 0x51;
  buf->writableData()[3] = 0xFF;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  buf->trimEnd(1);
  VLOG(10) << "Bad Index";
  buf->writableData()[2] = 0xBF;
  checkQError(decoder, buf->clone(), HPACK::DecodeError::BUFFER_UNDERFLOW);

  VLOG(10) << "Index static index";
  buf->writableData()[2] = 0xFF;
  buf->writableData()[3] = 0x7E;
  buf->append(1);
  checkQError(decoder, buf->clone(), HPACK::DecodeError::INVALID_INDEX);

  VLOG(10) << "No error after previous error";
  buf->writableData()[0] = 0xC1;
  buf->writableData()[1] = 0x01;
  buf->writableData()[2] = 0x41;
  buf->trimEnd(1);
  EXPECT_EQ(decoder.decodeEncoderStream(buf->clone()),
            HPACK::DecodeError::NONE);

  VLOG(10) << "Control decode error";
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

  VLOG(10) << "Bad header ack";
  EXPECT_EQ(encoder.decodeDecoderStream(buf->clone()),
            HPACK::DecodeError::INTEGER_OVERFLOW);

  VLOG(10) << "Bad cancel";
  buf->writableData()[0] = 0x7F;
  EXPECT_EQ(encoder.decodeDecoderStream(buf->clone()),
            HPACK::DecodeError::INTEGER_OVERFLOW);

  VLOG(10) << "Bad table state sync";
  buf->writableData()[0] = 0x3F;
  EXPECT_EQ(encoder.decodeDecoderStream(buf->clone()),
            HPACK::DecodeError::INTEGER_OVERFLOW);
}

TEST(QPACKContextTests, TestEvictedNameReference) {
  QPACKEncoder encoder(false, 109);
  QPACKDecoder decoder(109);
  encoder.setMaxVulnerable(0);
  vector<HPACKHeader> req;
  req.emplace_back("x-accept-encoding", "foobarfoobar");
  auto result = encoder.encode(req, 0, 1);
  decoder.decodeEncoderStream(std::move(result.control));
  decoder.decodeStreaming(1, result.stream->clone(),
                          result.stream->computeChainDataLength(), nullptr);
  encoder.onTableStateSync(1);
  req.clear();
  req.emplace_back("x-accept-encoding", "barfoobarfoo");
  result = encoder.encode(req, 0, 2);
  EXPECT_TRUE(stringInOutput(result.stream.get(), "x-accept-encoding"));
  TestStreamingCallback cb;
  decoder.decodeEncoderStream(std::move(result.control));
  decoder.decodeStreaming(2, result.stream->clone(),
                          result.stream->computeChainDataLength(), &cb);
  EXPECT_FALSE(cb.hasError());
}
