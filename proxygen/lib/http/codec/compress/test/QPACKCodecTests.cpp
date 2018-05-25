/*
 *  Copyright (c) 2018-present, Facebook, Inc.
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
#include <proxygen/lib/http/codec/compress/Header.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/QPACKCodec.h>
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

namespace {
void headersEq(vector<Header>& headerVec, compress::HeaderPieceList& headers) {
  size_t i = 0;
  EXPECT_EQ(headerVec.size() * 2, headers.size());
  for (auto& h: headerVec) {
    string name = *h.name;
    char *mutableName = (char *)name.data();
    folly::toLowerAscii(mutableName, name.size());
    EXPECT_EQ(name, headers[i++].str);
    EXPECT_EQ(*h.value, headers[i++].str);
  }
}
}

class QPACKTests : public testing::Test {
 public:

 protected:

  QPACKCodec client{TransportDirection::UPSTREAM};
  QPACKCodec server{TransportDirection::DOWNSTREAM};
};

TEST_F(QPACKTests, test_simple) {
  vector<Header> req = basicHeaders();
  auto encodeResult = client.encode(req, 1);
  ASSERT_NE(encodeResult.control.get(), nullptr);
  Cursor cCursor(encodeResult.control.get());
  EXPECT_EQ(server.decodeControl(cCursor, cCursor.totalLength()),
            HPACK::DecodeError::NONE);
  client.onControlHeaderAck();
  TestStreamingCallback cb;
  auto length = encodeResult.stream->computeChainDataLength();
  server.decodeStreaming(std::move(encodeResult.stream), length, &cb);
  client.onHeaderAck(1);
  auto result = cb.getResult();
  EXPECT_TRUE(result.isOk());
  headersEq(req, result.ok().headers);
}

TEST_F(QPACKTests, test_absolute_index) {
  int flights = 10;
  for (int i = 0; i < flights; i++) {
    vector<vector<string>> headers;
    for (int j = 0; j < 32; j++) {
      int value = (i >> 1) * 32 + j; // duplicate the last flight
      headers.emplace_back(
        vector<string>({string("foomonkey"), folly::to<string>(value)}));
    }
    auto req = headersFromArray(headers);
    auto encodeResult = client.encode(req, i + 1);
    Cursor cCursor(encodeResult.control.get());
    if (i % 2 == 1) {
      EXPECT_EQ(encodeResult.control.get(), nullptr);
    } else {
      ASSERT_NE(encodeResult.control.get(), nullptr);
      CHECK_EQ(server.decodeControl(cCursor, cCursor.totalLength()),
               HPACK::DecodeError::NONE);
      client.onControlHeaderAck();
    }
    TestStreamingCallback cb;
    auto length = encodeResult.stream->computeChainDataLength();
    server.decodeStreaming(std::move(encodeResult.stream), length, &cb);
    client.onHeaderAck(i + 1);
    auto result = cb.getResult();
    EXPECT_TRUE(result.isOk());
    headersEq(req, result.ok().headers);
  }
}

TEST_F(QPACKTests, test_with_queue) {
  // Sends 10 flights of 4 requests each
  // Each request contains two 'connection' headers, one with the current
  // index, and current index - 8.
  // Each flight is processed in the order 0, 3, 2, 1, unless an eviction
  // happens on 2 or 3, in which case we force an blocking event.
  vector<Header> req = basicHeaders();
  vector<string> values;
  int flights = 10;
  for (auto i = 0; i < flights * 4; i++) {
    values.push_back(folly::to<string>(i));
  }
  client.setEncoderHeaderTableSize(1024);
  for (auto f = 0; f < flights; f++) {
    vector<std::pair<unique_ptr<IOBuf>, TestStreamingCallback>> data;
    list<unique_ptr<IOBuf>> controlFrames;
    for (int i = 0; i < 4; i++) {
      auto reqI = req;
      for (int j = 0; j < 2; j++) {
        reqI.emplace_back(HTTP_HEADER_CONNECTION, values[
                            std::max(f * 4 + i - j * 8, 0)]);
      }
      VLOG(4) << "Encoding req=" << f * 4 + i;
      auto res = client.encode(reqI, f * 4 + i);
      if (res.control && res.control->computeChainDataLength() > 0) {
        controlFrames.emplace_back(std::move(res.control));
      }
      data.emplace_back(std::move(res.stream), TestStreamingCallback());
    }

    std::vector<int> insertOrder{0, 3, 2, 1};
    if (!controlFrames.empty()) {
      auto control = std::move(controlFrames.front());
      controlFrames.pop_front();
      Cursor c(control.get());
      server.decodeControl(c, c.totalLength());
      client.onControlHeaderAck();
    }
    for (auto i: insertOrder) {
      auto& encodedReq = data[i].first;
      auto len = encodedReq->computeChainDataLength();
      server.decodeStreaming(std::move(encodedReq), len, &data[i].second);
    }
    while (!controlFrames.empty()) {
      auto control = std::move(controlFrames.front());
      controlFrames.pop_front();
      Cursor c(control.get());
      server.decodeControl(c, c.totalLength());
      client.onControlHeaderAck();
    }
    int i = 0;
    for (auto& d: data) {
      auto result = d.second.getResult();
      EXPECT_TRUE(result.isOk());
      auto reqI = req;
      for (int j = 0; j < 2; j++) {
        reqI.emplace_back(HTTP_HEADER_CONNECTION,
                          values[std::max(f * 4 + i - j * 8, 0)]);
      }
      headersEq(reqI, result.ok().headers);
      client.onHeaderAck(f * 4 + i);
      i++;
    }
    VLOG(4) << "getHolBlockCount=" << server.getHolBlockCount();
  }
  // Skipping redundant table adds reduces the HOL block count
  EXPECT_EQ(server.getHolBlockCount(), 30);

}
