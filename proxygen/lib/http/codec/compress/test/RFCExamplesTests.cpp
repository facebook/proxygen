/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <algorithm>
#include <gtest/gtest.h>
#include <list>
#include <memory>
#include <proxygen/lib/http/codec/compress/HPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/HPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <proxygen/lib/http/codec/compress/test/TestUtil.h>
#include <vector>

using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;

class RFCExamplesTests : public testing::Test {
 public:
  RFCExamplesTests() {
    req1.push_back(HPACKHeader(":method", "GET"));
    req1.push_back(HPACKHeader(":scheme", "http"));
    req1.push_back(HPACKHeader(":path", "/"));
    req1.push_back(HPACKHeader(":authority", "www.example.com"));

    req2.push_back(HPACKHeader(":method", "GET"));
    req2.push_back(HPACKHeader(":scheme", "http"));
    req2.push_back(HPACKHeader(":path", "/"));
    req2.push_back(HPACKHeader(":authority", "www.example.com"));
    req2.push_back(HPACKHeader("cache-control", "no-cache"));

    req3.push_back(HPACKHeader(":method", "GET"));
    req3.push_back(HPACKHeader(":scheme", "https"));
    req3.push_back(HPACKHeader(":path", "/index.html"));
    req3.push_back(HPACKHeader(":authority", "www.example.com"));
    req3.push_back(HPACKHeader("custom-key", "custom-value"));

    resp1.push_back(HPACKHeader(":status", "302"));
    resp1.push_back(HPACKHeader("cache-control", "private"));
    resp1.push_back(HPACKHeader("date", "Mon, 21 Oct 2013 20:13:21 GMT"));
    resp1.push_back(HPACKHeader("location", "https://www.example.com"));

    resp2.push_back(HPACKHeader(":status", "200"));
    resp2.push_back(HPACKHeader("cache-control", "private"));
    resp2.push_back(HPACKHeader("date", "Mon, 21 Oct 2013 20:13:21 GMT"));
    resp2.push_back(HPACKHeader("location", "https://www.example.com"));

    resp3.push_back(HPACKHeader(":status", "200"));
    resp3.push_back(HPACKHeader("cache-control", "private"));
    resp3.push_back(HPACKHeader("date", "Mon, 21 Oct 2013 20:13:22 GMT"));
    resp3.push_back(HPACKHeader("location", "https://www.example.com"));
    resp3.push_back(HPACKHeader("content-encoding", "gzip"));
    resp3.push_back(
      HPACKHeader(
        "set-cookie",
        "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1")
    );
  }

 protected:
  vector<HPACKHeader> req1;
  vector<HPACKHeader> req2;
  vector<HPACKHeader> req3;
  vector<HPACKHeader> resp1;
  vector<HPACKHeader> resp2;
  vector<HPACKHeader> resp3;
};

TEST_F(RFCExamplesTests, rfc_example_e2_request_no_huffman) {
  HPACKEncoder encoder(HPACK::MessageType::REQ, false);
  HPACKDecoder decoder(HPACK::MessageType::REQ);
  // first request
  unique_ptr<IOBuf> encoded = hpack::encodeDecode(req1, encoder, decoder);
  EXPECT_EQ(encoded->length(), 20);
  EXPECT_EQ(encoder.getTable().bytes(), 180);
  EXPECT_EQ(encoder.getTable().size(), 4);
  auto refset = encoder.getTable().referenceSet();
  EXPECT_EQ(refset.size(), 4);

  // second request
  encoded = hpack::encodeDecode(req2, encoder, decoder);
  EXPECT_EQ(encoded->length(), 10);
  EXPECT_EQ(encoder.getTable().bytes(), 233);
  EXPECT_EQ(encoder.getTable().size(), 5);
  refset = encoder.getTable().referenceSet();
  EXPECT_EQ(refset.size(), 5);

  // third request
  encoded = hpack::encodeDecode(req3, encoder, decoder);
  EXPECT_EQ(encoded->length(), 30);
  EXPECT_EQ(encoder.getTable().bytes(), 379);
  EXPECT_EQ(encoder.getTable().size(), 8);
  refset = encoder.getTable().referenceSet();
  EXPECT_EQ(decoder.getTable().referenceSet(), refset);
}

TEST_F(RFCExamplesTests, rfc_example_e3_request_with_huffman) {
  HPACKEncoder encoder(HPACK::MessageType::REQ, true);
  HPACKDecoder decoder(HPACK::MessageType::REQ);

  // first
  unique_ptr<IOBuf> encoded = hpack::encodeDecode(req1, encoder, decoder);
  EXPECT_EQ(encoded->length(), 16);
  EXPECT_EQ(encoder.getTable().bytes(), 180);
  EXPECT_EQ(encoder.getTable().size(), 4);
  // verify the last byte
  EXPECT_EQ(encoded->data()[15], 0x7F);
  EXPECT_EQ(decoder.getTable().bytes(), 180);
  EXPECT_EQ(decoder.getTable().size(), 4);
  auto refset = decoder.getTable().referenceSet();
  EXPECT_EQ(refset.size(), 4);

  // second
  encoded = hpack::encodeDecode(req2, encoder, decoder);
  EXPECT_EQ(encoded->length(), 8);
  EXPECT_EQ(encoder.getTable().bytes(), 233);
  EXPECT_EQ(encoder.getTable().size(), 5);
  refset = decoder.getTable().referenceSet();
  EXPECT_EQ(refset.size(), 5);

  // third
  encoded = hpack::encodeDecode(req3, encoder, decoder);
  EXPECT_EQ(encoded->length(), 25);
  EXPECT_EQ(encoder.getTable().bytes(), 379);
  EXPECT_EQ(encoder.getTable().size(), 8);
  refset = decoder.getTable().referenceSet();
  EXPECT_EQ(refset.size(), 5);
}

TEST_F(RFCExamplesTests, rfc_example_e4_response_no_huffman) {
  // this test does some evictions
  uint32_t tableSize = 256;
  HPACKEncoder encoder(HPACK::MessageType::RESP, false, tableSize);
  HPACKDecoder decoder(HPACK::MessageType::RESP, tableSize);

  // first
  unique_ptr<IOBuf> encoded = hpack::encodeDecode(resp1, encoder, decoder);
  EXPECT_EQ(encoded->length(), 70);
  EXPECT_EQ(encoder.getTable().bytes(), 222);
  EXPECT_EQ(encoder.getTable().size(), 4);
  auto refset = encoder.getTable().referenceSet();
  EXPECT_EQ(refset.size(), 4);

  // second
  encoded = hpack::encodeDecode(resp2, encoder, decoder);
  EXPECT_EQ(encoded->length(), 2);
  EXPECT_EQ(encoded->data()[0], 0x84);
  EXPECT_EQ(encoded->data()[1], 0x8c);
  EXPECT_EQ(encoder.getTable().bytes(), 222);
  EXPECT_EQ(encoder.getTable().size(), 4);
  refset = encoder.getTable().referenceSet();
  EXPECT_EQ(refset.size(), 4);

  // third
  encoded = hpack::encodeDecode(resp3, encoder, decoder);
  EXPECT_EQ(encoded->length(), 102);
  EXPECT_EQ(encoded->data()[0], 0x83);
  // this sequence of two identical bytes is the reference eviction
  EXPECT_EQ(encoded->data()[1], 0x84);
  EXPECT_EQ(encoded->data()[2], 0x84);
  // last byte
  EXPECT_EQ(encoded->data()[101], 0x31);

  EXPECT_EQ(encoder.getTable().size(), 3);
  EXPECT_EQ(encoder.getTable().bytes(), 215);
}

TEST_F(RFCExamplesTests, rfc_example_e5_response_with_huffman) {
  uint32_t tableSize = 256;
  HPACKEncoder encoder(HPACK::MessageType::RESP, true, tableSize);
  HPACKDecoder decoder(HPACK::MessageType::RESP, tableSize);

  // first
  unique_ptr<IOBuf> encoded = hpack::encodeDecode(resp1, encoder, decoder);
  EXPECT_EQ(encoded->length(), 53);
  EXPECT_EQ(encoded->data()[52], 0xFF);
  EXPECT_EQ(encoder.getTable().size(), 4);
  EXPECT_EQ(encoder.getTable().bytes(), 222);
  auto refset = encoder.getTable().referenceSet();
  EXPECT_EQ(refset.size(), 4);

  // second
  encoded = hpack::encodeDecode(resp2, encoder, decoder);
  EXPECT_EQ(encoded->length(), 2);
  EXPECT_EQ(encoded->data()[0], 0x84);
  EXPECT_EQ(encoded->data()[1], 0x8c);
  EXPECT_EQ(encoder.getTable().bytes(), 222);
  EXPECT_EQ(encoder.getTable().size(), 4);
  refset = encoder.getTable().referenceSet();
  EXPECT_EQ(refset.size(), 4);

  // third
  encoded = hpack::encodeDecode(resp3, encoder, decoder);
  EXPECT_EQ(encoded->length(), 86);
  EXPECT_EQ(encoded->data()[0], 0x83);
  // this sequence of two identical bytes is the reference eviction
  EXPECT_EQ(encoded->data()[1], 0x84);
  EXPECT_EQ(encoded->data()[2], 0x84);
  // last byte
  EXPECT_EQ(encoded->data()[85], 0x2F);

  EXPECT_EQ(encoder.getTable().size(), 3);
  EXPECT_EQ(encoder.getTable().bytes(), 215);
}
