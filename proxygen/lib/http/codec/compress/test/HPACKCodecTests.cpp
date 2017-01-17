/*
 *  Copyright (c) 2017, Facebook, Inc.
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
#include <proxygen/lib/http/codec/compress/Header.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <vector>

using namespace folly::io;
using namespace folly;
using namespace proxygen::compress;
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

class TestHeaderCodecStats : public HeaderCodec::Stats {

 public:
  void recordEncode(HeaderCodec::Type type, HTTPHeaderSize& size) override {
    EXPECT_EQ(type, HeaderCodec::Type::HPACK);
    encodes++;
    encodedBytesCompr += size.compressed;
    encodedBytesUncompr += size.uncompressed;
  }

  void recordDecode(HeaderCodec::Type type, HTTPHeaderSize& size) override {
    EXPECT_EQ(type, HeaderCodec::Type::HPACK);
    decodes++;
    decodedBytesCompr += size.compressed;
    decodedBytesUncompr += size.uncompressed;
  }

  void recordDecodeError(HeaderCodec::Type type) override {
    EXPECT_EQ(type, HeaderCodec::Type::HPACK);
    errors++;
  }

  void recordDecodeTooLarge(HeaderCodec::Type type) override {
    EXPECT_EQ(type, HeaderCodec::Type::HPACK);
    errors++; //?
  }

  void reset() {
    encodes = 0;
    decodes = 0;
    encodedBytesCompr = 0;
    encodedBytesUncompr = 0;
    decodedBytesCompr = 0;
    decodedBytesUncompr = 0;
    errors = 0;
  }

  uint32_t encodes{0};
  uint32_t encodedBytesCompr{0};
  uint32_t encodedBytesUncompr{0};
  uint32_t decodes{0};
  uint32_t decodedBytesCompr{0};
  uint32_t decodedBytesUncompr{0};
  uint32_t errors{0};
};

class HPACKCodecTests : public testing::Test {

 protected:
  vector<Header> headersFromArray(vector<vector<string>>& a) {
    vector<Header> headers;
    for (auto& ha : a) {
      headers.push_back(Header(ha[0], ha[1]));
    }
    return headers;
  }

  HPACKCodec client{TransportDirection::UPSTREAM};
  HPACKCodec server{TransportDirection::DOWNSTREAM};
};

TEST_F(HPACKCodecTests, request) {
  vector<vector<string>> headers = {
    {":path", "/index.php"},
    {":host", "www.facebook.com"},
    {"accept-encoding", "gzip"}
  };
  vector<Header> req = headersFromArray(headers);

  for (int i = 0; i < 3; i++) {
    unique_ptr<IOBuf> encodedReq = client.encode(req);
    Cursor cursor(encodedReq.get());
    uint32_t len = 0;
    if (encodedReq) {
      len = encodedReq->computeChainDataLength();
    }
    auto result = server.decode(cursor, len);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.ok().headers.size(), 6);
  }
}

TEST_F(HPACKCodecTests, response) {
  vector<vector<string>> headers = {
    {"content-length", "80"},
    {"content-encoding", "gzip"},
    {"x-fb-debug", "sdfgrwer"}
  };
  vector<Header> req = headersFromArray(headers);

  for (int i = 0; i < 3; i++) {
    unique_ptr<IOBuf> encodedReq = server.encode(req);
    Cursor cursor(encodedReq.get());
    uint32_t len = 0;
    if (encodedReq) {
      len = encodedReq->computeChainDataLength();
    }
    auto result = client.decode(cursor, len);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.ok().headers.size(), 6);
  }
}

TEST_F(HPACKCodecTests, headroom) {
  vector<vector<string>> headers = {
    {":path", "/index.php"},
    {":host", "www.facebook.com"},
    {"accept-encoding", "gzip"}
  };
  vector<Header> req = headersFromArray(headers);

  uint32_t headroom = 20;
  client.setEncodeHeadroom(headroom);
  for (int i = 0; i < 3; i++) {
    unique_ptr<IOBuf> encodedReq = client.encode(req);
    EXPECT_EQ(encodedReq->headroom(), headroom);
    Cursor cursor(encodedReq.get());
    uint32_t len = 0;
    if (encodedReq) {
      len = encodedReq->computeChainDataLength();
    }
    auto result = server.decode(cursor, len);
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(result.ok().headers.size(), 6);
  }
}

/**
 * makes sure that the encoder will lowercase the header names
 */
TEST_F(HPACKCodecTests, lowercasing_header_names) {
  vector<vector<string>> headers = {
    {"Content-Length", "80"},
    {"Content-Encoding", "gzip"},
    {"X-FB-Debug", "bleah"}
  };
  vector<Header> req = headersFromArray(headers);

  unique_ptr<IOBuf> encodedReq = server.encode(req);
  Cursor cursor(encodedReq.get());
  uint32_t len = 0;
  if (encodedReq) {
    len = encodedReq->computeChainDataLength();
  }
  auto result = client.decode(cursor, len);
  EXPECT_TRUE(result.isOk());
  auto& decoded = result.ok().headers;
  CHECK_EQ(decoded.size(), 6);
  for (int i = 0; i < 6; i+=2) {
    EXPECT_TRUE(isLowercase(decoded[i].str));
  }
}

/**
 * make sure we mark multi-valued headers appropriately,
 * as expected by the SPDY codec.
 */
TEST_F(HPACKCodecTests, multivalue_headers) {
  vector<vector<string>> headers = {
    {"Content-Length", "80"},
    {"Content-Encoding", "gzip"},
    {"X-FB-Dup", "bleah"},
    {"X-FB-Dup", "hahaha"}
  };
  vector<Header> req = headersFromArray(headers);

  unique_ptr<IOBuf> encodedReq = server.encode(req);
  Cursor cursor(encodedReq.get());
  uint32_t len = 0;
  if (encodedReq) {
    len = encodedReq->computeChainDataLength();
  }
  auto result = client.decode(cursor, len);
  EXPECT_TRUE(result.isOk());
  auto& decoded = result.ok().headers;
  CHECK_EQ(decoded.size(), 8);
  for (int i = 0; i < 6; i+=2) {
    if (decoded[i].str == "x-fb-dup") {
      EXPECT_TRUE(decoded[i].isMultiValued());
    }
  }
}

/**
 * test that we're propagating the error correctly in the decoder
 */
TEST_F(HPACKCodecTests, decode_error) {
  vector<vector<string>> headers = {
    {"Content-Length", "80"}
  };
  vector<Header> req = headersFromArray(headers);

  unique_ptr<IOBuf> encodedReq = server.encode(req);
  // mangle the buffer to force an error
  uint32_t len = encodedReq->computeChainDataLength();
  encodedReq->writableData()[0] = 0xFF;
  Cursor cursor(encodedReq.get());

  TestHeaderCodecStats stats;
  client.setStats(&stats);
  auto result = client.decode(cursor, len);
  // this means there was an error
  EXPECT_TRUE(result.isError());
  EXPECT_EQ(result.error(), HeaderDecodeError::BAD_ENCODING);
  EXPECT_EQ(stats.errors, 1);
  client.setStats(nullptr);
}

/**
 * testing that we're calling the stats callbacks appropriately
 */
TEST_F(HPACKCodecTests, header_codec_stats) {
  vector<vector<string>> headers = {
    {"Content-Length", "80"},
    {"Content-Encoding", "gzip"},
    {"X-FB-Debug", "eirtijvdgtccffkutnbttcgbfieghgev"}
  };
  vector<Header> resp = headersFromArray(headers);

  TestHeaderCodecStats stats;
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
  uint32_t len = 0;
  if (encodedResp) {
    len = encodedResp->computeChainDataLength();
  }
  stats.reset();
  client.setStats(&stats);
  auto result = client.decode(cursor, len);
  EXPECT_TRUE(result.isOk());
  auto& decoded = result.ok().headers;
  CHECK_EQ(decoded.size(), 3 * 2);
  EXPECT_EQ(stats.decodes, 1);
  EXPECT_EQ(stats.encodes, 0);
  EXPECT_TRUE(stats.decodedBytesCompr > 0);
  EXPECT_TRUE(stats.decodedBytesUncompr > 0);
  EXPECT_EQ(stats.encodedBytesCompr, 0);
  EXPECT_EQ(stats.encodedBytesUncompr, 0);
  client.setStats(nullptr);
}

/**
 * check that we're enforcing the limit on total uncompressed size
 */
TEST_F(HPACKCodecTests, uncompressed_size_limit) {
  vector<vector<string>> headers;
  // generate lots of small headers
  string contentLength = "Content-Length";
  for (int i = 0; i < 10000; i++) {
    string value = folly::to<string>(i);
    vector<string> header = {contentLength, value};
    headers.push_back(header);
  }
  vector<Header> req = headersFromArray(headers);
  unique_ptr<IOBuf> encoded = server.encode(req);
  Cursor cursor(encoded.get());
  uint32_t len = 0;
  if (encoded) {
    len = encoded->computeChainDataLength();
  }
  auto result = client.decode(cursor, len);
  EXPECT_TRUE(result.isError());
  EXPECT_EQ(result.error(), HeaderDecodeError::HEADERS_TOO_LARGE);
}
