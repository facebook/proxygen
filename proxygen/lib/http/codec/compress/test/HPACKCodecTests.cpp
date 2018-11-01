/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Range.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <glog/logging.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/codec/compress/HPACKCodec.h>
#include <proxygen/lib/http/codec/compress/HPACKQueue.h>
#include <proxygen/lib/http/codec/compress/Header.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/test/TestStreamingCallback.h>
#include <proxygen/lib/http/codec/compress/test/TestUtil.h>
#include <vector>

using namespace folly::io;
using namespace folly;
using namespace proxygen::compress;
using namespace proxygen::hpack;
using namespace proxygen;
using namespace std;
using namespace testing;

bool isLowercase(StringPiece str) {
  for (auto ch : str) {
    if (isalpha(ch) && !islower(ch)) {
      return false;
    }
  }
  return true;
}

namespace {

struct DecodeResult {
  compress::HeaderPieceList headers;
  uint32_t bytesConsumed;
};

folly::Expected<DecodeResult, HPACK::DecodeError>
decode(HPACKCodec& codec, Cursor& cursor, uint32_t length) noexcept {
  TestStreamingCallback cb;
  codec.decodeStreaming(cursor, length, &cb);
  if (cb.hasError()) {
    LOG(ERROR) << "decoder state: " << codec;
    return folly::makeUnexpected(cb.error);
  }
  return DecodeResult{std::move(cb.getResult()->headers),
      cb.getResult()->bytesConsumed};
}

folly::Expected<DecodeResult, HPACK::DecodeError>
encodeDecode(HPACKCodec& encoder, HPACKCodec& decoder,
             vector<Header>&& headers) {
  unique_ptr<IOBuf> encoded = encoder.encode(headers);
  Cursor c(encoded.get());
  return decode(decoder, c, c.totalLength());
}

uint64_t bufLen(const std::unique_ptr<IOBuf>& buf) {
  if (buf) {
    return buf->computeChainDataLength();
  }
  return 0;
}

}

class HPACKCodecTests : public testing::Test {
 protected:

  HPACKCodec client{TransportDirection::UPSTREAM};
  HPACKCodec server{TransportDirection::DOWNSTREAM};
};

TEST_F(HPACKCodecTests, Request) {
  for (int i = 0; i < 3; i++) {
    auto result = encodeDecode(client, server, basicHeaders());
    EXPECT_TRUE(!result.hasError());
    EXPECT_EQ(result->headers.size(), 12);
  }
}

TEST_F(HPACKCodecTests, Response) {
  vector<vector<string>> headers = {
    {"content-length", "80"},
    {"content-encoding", "gzip"},
    {"x-fb-debug", "sdfgrwer"}
  };
  vector<Header> req = headersFromArray(headers);

  for (int i = 0; i < 3; i++) {
    auto result = encodeDecode(server, client, basicHeaders());
    EXPECT_TRUE(!result.hasError());
    EXPECT_EQ(result->headers.size(), 12);
  }
}

TEST_F(HPACKCodecTests, Headroom) {
  vector<Header> req = basicHeaders();

  uint32_t headroom = 20;
  client.setEncodeHeadroom(headroom);
  unique_ptr<IOBuf> encodedReq = client.encode(req);
  EXPECT_EQ(encodedReq->headroom(), headroom);
  Cursor cursor(encodedReq.get());
  auto result = decode(server, cursor, cursor.totalLength());
  EXPECT_TRUE(!result.hasError());
  EXPECT_EQ(result->headers.size(), 12);
}

/**
 * makes sure that the encoder will lowercase the header names
 */
TEST_F(HPACKCodecTests, LowercasingHeaderNames) {
  vector<vector<string>> headers = {
    {"Content-Length", "80"},
    {"Content-Encoding", "gzip"},
    {"X-FB-Debug", "bleah"}
  };
  auto result = encodeDecode(server, client, headersFromArray(headers));
  EXPECT_TRUE(!result.hasError());
  auto& decoded = result->headers;
  CHECK_EQ(decoded.size(), 6);
  for (int i = 0; i < 6; i += 2) {
    EXPECT_TRUE(isLowercase(decoded[i].str));
  }
}

/**
 * make sure we mark multi-valued headers appropriately,
 * as expected by the SPDY codec.
 */
TEST_F(HPACKCodecTests, MultivalueHeaders) {
  vector<vector<string>> headers = {
    {"Content-Length", "80"},
    {"Content-Encoding", "gzip"},
    {"X-FB-Dup", "bleah"},
    {"X-FB-Dup", "hahaha"}
  };
  auto result = encodeDecode(server, client, headersFromArray(headers));
  EXPECT_TRUE(!result.hasError());
  auto& decoded = result->headers;
  CHECK_EQ(decoded.size(), 8);
  uint32_t count = 0;
  for (int i = 0; i < 8; i += 2) {
    if (decoded[i].str == "x-fb-dup") {
      count++;
    }
  }
  EXPECT_EQ(count, 2);
}

/**
 * test that we're propagating the error correctly in the decoder
 */
TEST_F(HPACKCodecTests, DecodeError) {
  vector<vector<string>> headers = {
    {"Content-Length", "80"}
  };
  vector<Header> req = headersFromArray(headers);

  unique_ptr<IOBuf> encodedReq = server.encode(req);
  encodedReq->writableData()[0] = 0xFF;
  Cursor cursor(encodedReq.get());

  TestHeaderCodecStats stats(HeaderCodec::Type::HPACK);
  client.setStats(&stats);
  auto result = decode(client, cursor, cursor.totalLength());
  // this means there was an error
  EXPECT_TRUE(result.hasError());
  EXPECT_EQ(result.error(), HPACK::DecodeError::INVALID_INDEX);
  EXPECT_EQ(stats.errors, 1);
  client.setStats(nullptr);
}

/**
 * testing that we're calling the stats callbacks appropriately
 */
TEST_F(HPACKCodecTests, HeaderCodecStats) {
  vector<vector<string>> headers = {
    {"Content-Length", "80"},
    {"Content-Encoding", "gzip"},
    {"X-FB-Debug", "eirtijvdgtccffkutnbttcgbfieghgev"}
  };
  vector<Header> resp = headersFromArray(headers);

  TestHeaderCodecStats stats(HeaderCodec::Type::HPACK);
  // encode
  server.setStats(&stats);
  unique_ptr<IOBuf> encodedResp = server.encode(resp);
  EXPECT_EQ(stats.encodes, 1);
  EXPECT_EQ(stats.decodes, 0);
  EXPECT_EQ(stats.errors, 0);
  EXPECT_TRUE(stats.encodedBytesCompr > 0);
  EXPECT_TRUE(stats.encodedBytesUncompr > 0);
  EXPECT_EQ(stats.decodedBytesCompr, 0);
  EXPECT_EQ(stats.decodedBytesUncompr, 0);
  server.setStats(nullptr);

  // decode
  Cursor cursor(encodedResp.get());
  stats.reset();
  client.setStats(&stats);
  auto result = decode(client, cursor, cursor.totalLength());
  EXPECT_TRUE(!result.hasError());
  auto& decoded = result->headers;
  CHECK_EQ(decoded.size(), 3 * 2);
  EXPECT_EQ(stats.decodes, 1);
  EXPECT_EQ(stats.encodes, 0);
  EXPECT_GT(stats.decodedBytesCompr, 0);
  EXPECT_GT(stats.decodedBytesUncompr, 0);
  EXPECT_EQ(stats.encodedBytesCompr, 0);
  EXPECT_EQ(stats.encodedBytesUncompr, 0);
  client.setStats(nullptr);
}

/**
 * check that we're enforcing the limit on total uncompressed size
 */
TEST_F(HPACKCodecTests, UncompressedSizeLimit) {
  vector<vector<string>> headers;
  // generate lots of small headers
  string contentLength = "Content-Length";
  for (int i = 0; i < 10000; i++) {
    string value = folly::to<string>(i);
    vector<string> header = {contentLength, value};
    headers.push_back(header);
  }
  auto result = encodeDecode(server, client, headersFromArray(headers));
  EXPECT_TRUE(result.hasError());
  EXPECT_EQ(result.error(), HPACK::DecodeError::HEADERS_TOO_LARGE);
}


/**
 * Size limit stats
 */
TEST_F(HPACKCodecTests, SizeLimitStats) {
  vector<vector<string>> headers;
  // generate lots of small headers
  string contentLength = "Content-Length";
  for (int i = 0; i < 10000; i++) {
    string value = folly::to<string>(i);
    vector<string> header = {contentLength, value};
    headers.push_back(header);
  }
  auto encHeaders = headersFromArray(headers);
  unique_ptr<IOBuf> encoded = client.encode(encHeaders);
  Cursor cursor(encoded.get());
  TestStreamingCallback cb;
  TestHeaderCodecStats stats(HeaderCodec::Type::HPACK);
  server.setStats(&stats);
  server.decodeStreaming(cursor, cursor.totalLength(), &cb);
  auto result = cb.getResult();
  EXPECT_TRUE(result.hasError());
  EXPECT_EQ(result.error(), HPACK::DecodeError::HEADERS_TOO_LARGE);
  EXPECT_EQ(stats.tooLarge, 1);
}

TEST_F(HPACKCodecTests, DefaultHeaderIndexingStrategy) {
  vector<Header> headers = basicHeaders();
  size_t headersIndexableSize = 4;

  // Control equality check; all basic headers were indexed
  client.encode(headers);
  EXPECT_EQ(client.getHPACKTableInfo().egressHeadersStored_,
            headersIndexableSize);

  // Verify HPACKCodec by default utilizes the default header indexing strategy
  // by ensuring that it does not index any of the added headers below
  // The below is quite verbose but that is because Header constructors use
  // references and so we need the actual strings to not go out of scope
  vector<vector<string>> noIndexHeadersStrings = {
    {"content-length", "80"},
    {":path", "/some/random/file.jpg"},
    {":path", "checks_for_="},
    {"if-modified-since", "some_value"},
    {"last-modified", "some_value"}
  };
  vector<Header> noIndexHeaders = headersFromArray(noIndexHeadersStrings);
  headers.insert(headers.end(), noIndexHeaders.begin(), noIndexHeaders.end());
  HPACKCodec testCodec{TransportDirection::UPSTREAM};
  testCodec.encode(headers);
  EXPECT_EQ(
    testCodec.getHPACKTableInfo().egressHeadersStored_, headersIndexableSize);
}


class HPACKQueueTests : public testing::TestWithParam<int> {
 public:
  HPACKQueueTests()
      : queue(std::make_unique<HPACKQueue>(server)) {}

 protected:

  HPACKCodec client{TransportDirection::UPSTREAM};
  HPACKCodec server{TransportDirection::DOWNSTREAM};
  std::unique_ptr<HPACKQueue> queue;
};

TEST_F(HPACKQueueTests, QueueInline) {
  vector<Header> req = basicHeaders();
  TestStreamingCallback cb;

  for (int i = 0; i < 3; i++) {
    unique_ptr<IOBuf> encodedReq = client.encode(req);
    auto len = bufLen(encodedReq);
    cb.reset();
    queue->enqueueHeaderBlock(i, std::move(encodedReq), len, &cb, false);
    auto result = cb.getResult();
    EXPECT_TRUE(!result.hasError());
    EXPECT_EQ(result->headers.size(), 12);
  }
}

TEST_F(HPACKQueueTests, QueueReorder) {
  vector<Header> req = basicHeaders();
  vector<std::pair<unique_ptr<IOBuf>, TestStreamingCallback>> data;

  for (int i = 0; i < 4; i++) {
    data.emplace_back(client.encode(req), TestStreamingCallback());
  }

  std::vector<int> insertOrder{1, 3, 2, 0};
  for (auto i: insertOrder) {
    auto& encodedReq = data[i].first;
    auto len = bufLen(encodedReq);
    queue->enqueueHeaderBlock(i, std::move(encodedReq), len, &data[i].second,
                             false);
  }
  for (auto& d: data) {
    auto result = d.second.getResult();
    EXPECT_TRUE(!result.hasError());
    EXPECT_EQ(result->headers.size(), 12);
  }
  EXPECT_EQ(queue->getHolBlockCount(), 3);
}

TEST_F(HPACKQueueTests, QueueReorderOoo) {
  vector<Header> req = basicHeaders();
  vector<std::pair<unique_ptr<IOBuf>, TestStreamingCallback>> data;

  for (int i = 0; i < 4; i++) {
    data.emplace_back(client.encode(req), TestStreamingCallback());
  }

  std::vector<int> insertOrder{0, 3, 2, 1};
  for (auto i: insertOrder) {
    auto& encodedReq = data[i].first;
    auto len = bufLen(encodedReq);
    // Allow idx 3 to be decoded out of order
    queue->enqueueHeaderBlock(i, std::move(encodedReq), len, &data[i].second,
                              i == 3);
  }
  for (auto& d: data) {
    auto result = d.second.getResult();
    EXPECT_TRUE(!result.hasError());
    EXPECT_EQ(result->headers.size(), 12);
  }
  EXPECT_EQ(queue->getHolBlockCount(), 1);
}

TEST_F(HPACKQueueTests, QueueError) {
  vector<Header> req = basicHeaders();
  TestStreamingCallback cb;

  bool expectOk = true;
  // ok, dup, ok, lower
  for (auto i: std::vector<int>({0, 0, 1, 0, 3, 3, 2})) {
    unique_ptr<IOBuf> encodedReq = client.encode(req);
    auto len = bufLen(encodedReq);
    cb.reset();
    queue->enqueueHeaderBlock(i, std::move(encodedReq), len, &cb, true);
    auto result = cb.getResult();
    if (expectOk) {
      EXPECT_TRUE(!result.hasError());
      EXPECT_EQ(result->headers.size(), 12);
    } else {
      EXPECT_TRUE(result.hasError());
      EXPECT_EQ(result.error(), HPACK::DecodeError::BAD_SEQUENCE_NUMBER);
    }
    expectOk = !expectOk;
  }
}

TEST_P(HPACKQueueTests, QueueDeleted) {
  vector<Header> req = basicHeaders();
  vector<std::pair<unique_ptr<IOBuf>, TestStreamingCallback>> data;

  for (int i = 0; i < 4; i++) {
    data.emplace_back(client.encode(req), TestStreamingCallback());
    if (i == GetParam()) {
      data.back().second.headersCompleteCb = [&] { queue.reset(); };
    }
  }

  std::vector<int> insertOrder{0, 3, 2, 1};
  for (auto i: insertOrder) {
    auto& encodedReq = data[i].first;
    auto len = bufLen(encodedReq);

    // Allow idx 3 to be decoded out of order
    queue->enqueueHeaderBlock(i, std::move(encodedReq), len, &data[i].second,
                             i == 3);
    if (!queue) {
      break;
    }
  }
}

INSTANTIATE_TEST_CASE_P(Queue,
                        HPACKQueueTests,
                        ::testing::Values(0, 1, 2, 3));
