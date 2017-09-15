/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/portability/GTest.h>
#include <memory>
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKHeaderTable.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <sstream>

using namespace std;
using namespace testing;

namespace proxygen {

class QPACKHeaderTableTests : public testing::Test {
 protected:
  QPACKHeaderTable table{4096};
};

TEST_F(QPACKHeaderTableTests, add) {
  HPACKHeaderName acceptEncoding("accept-encoding");
  EXPECT_TRUE(table.add(HPACKHeader("accept-encoding", "gzip"), 1));
  EXPECT_TRUE(table.add(HPACKHeader("accept-encoding", "gzip"), 2));
  EXPECT_TRUE(table.add(HPACKHeader("accept-encoding", "gzip"), 3));
  EXPECT_EQ(table.names().size(), 1);
  EXPECT_EQ(table.hasName(acceptEncoding), true);
  auto it = table.names().find(acceptEncoding);
  EXPECT_EQ(it->second.size(), 3);
  EXPECT_EQ(table.nameIndexRef(acceptEncoding), 1);
  EXPECT_EQ(table.getIndex(HPACKHeader("blarf", "blarg")), 0);
}

TEST_F(QPACKHeaderTableTests, addDup) {
  EXPECT_TRUE(table.add(HPACKHeader("accept-encoding", "gzip"), 1));
  EXPECT_TRUE(table.add(HPACKHeader("accept-encoding", "gzip"), 1));
  // value is different, error
  EXPECT_FALSE(table.add(HPACKHeader("accept-encoding", "br"), 1));
  EXPECT_EQ(table.size(), 1);
}

TEST_F(QPACKHeaderTableTests, addExceedCapacity) {
  HPACKHeader h("a", "b");
  for (uint32_t i = 0; i < (4096 / h.bytes()); i++) {
    EXPECT_TRUE(table.add(h, i + 1));
  }
  EXPECT_FALSE(table.add(h, 1000));
}

TEST_F(QPACKHeaderTableTests, encodeDecode) {
  EXPECT_TRUE(table.add(HPACKHeader("accept-encoding", "gzip"), 1));
  EXPECT_EQ(table.getIndexRef(HPACKHeader("accept-encoding", "gzip")), 1);
  EXPECT_EQ(table.nameIndexRef(HPACKHeaderName("accept-encoding")), 1);

  QPACKHeaderTable decoderTable(4096);
  uint32_t decoded = 0;
  decoderTable.add(HPACKHeader("accept-encoding", "gzip"), 1);
  decoderTable.decodeIndexRef(1)
    .then([&decoded] (QPACKHeaderTable::DecodeResult res) {
        EXPECT_EQ(res.which, 0);
        EXPECT_EQ(res.ref, HPACKHeader("accept-encoding", "gzip"));
        decoded++;
      });
  decoderTable.decodeIndexRef(1)
    .then([&decoded] (QPACKHeaderTable::DecodeResult res) {
        EXPECT_EQ(res.which, 0);
        EXPECT_EQ(res.ref, HPACKHeader("accept-encoding", "gzip"));
        decoded++;
      });

  auto res = table.encoderRemove(1);
  EXPECT_EQ(res.first, 3);
  decoderTable.decoderRemove(1, res.first)
    .then([this] { table.encoderRemoveAck(1); });
  EXPECT_EQ(decoded, 2);
  EXPECT_EQ(table.size(), 0);
  EXPECT_EQ(decoderTable.size(), 0);
}


TEST_F(QPACKHeaderTableTests, encoderRemove) {
  HPACKHeader h("accept-encoding", "gzip");
  table.add(h, 1);
  auto res = table.encoderRemove(1);
  EXPECT_EQ(table.pendingDeleteBytes(), h.bytes());
  EXPECT_EQ(table.getIndexRef(h), 0);
  EXPECT_EQ(table.nameIndexRef(h.name), 0);
  EXPECT_EQ(res.first, 1);
  bool removed = false;
  res.second.then([&removed] { removed = true; });
  table.encoderRemoveAck(1);
  EXPECT_EQ(table.pendingDeleteBytes(), 0);
  EXPECT_TRUE(removed);
}

TEST_F(QPACKHeaderTableTests, decoderRemoveImmediate) {
  // delRefCount equal, immediate removal
  table.add(HPACKHeader("accept-encoding", "gzip"), 1);
  bool removed = false;
  table.decoderRemove(1, 1)
    .then([&removed] { removed = true; });
  EXPECT_TRUE(removed);
}


TEST_F(QPACKHeaderTableTests, decoderRemoveDelayed) {
  // delRecount higher, delayed removal
  table.add(HPACKHeader("accept-encoding", "br"), 1);
  bool removed = false;
  table.decoderRemove(1, 2)
    .then([&removed] { removed = true; });
  EXPECT_FALSE(removed);
  bool decoded = false;
  table.decodeIndexRef(1)
    .then([&decoded] (QPACKHeaderTable::DecodeResult result) {
        EXPECT_EQ(result.which, 1);
        EXPECT_EQ(result.value, HPACKHeader("accept-encoding", "br"));
        decoded = true;
      });
  EXPECT_TRUE(decoded);
  EXPECT_TRUE(removed);
}

TEST_F(QPACKHeaderTableTests, decoderRemoveBeforeAdd) {
  // delete before add
  bool removed = false;
  table.decoderRemove(1, 1)
    .then([&removed] { removed = true; });
  EXPECT_FALSE(removed);

  // It's not in the table after add because of pending delete
  EXPECT_FALSE(table.add(HPACKHeader("accept-encoding", "br"), 1));
  EXPECT_TRUE(removed);
}


TEST_F(QPACKHeaderTableTests, decoderRemoveBeforeDecodeAdd) {
  // delete, decode, add
  bool removed = false;
  table.decoderRemove(1, 2)
    .then([&removed] { removed = true; });
  EXPECT_FALSE(removed);

  bool decoded = false;
  table.decodeIndexRef(1)
    .then([&decoded] (QPACKHeaderTable::DecodeResult result) {
        EXPECT_EQ(result.which, 1);
        EXPECT_EQ(result.value, result.ref);
        EXPECT_EQ(result.value, HPACKHeader("accept-encoding", "br"));
        decoded = true;
      });
  EXPECT_FALSE(decoded);

  // It's not in the table after add because of pending delete
  EXPECT_FALSE(table.add(HPACKHeader("accept-encoding", "br"), 1));
  EXPECT_TRUE(decoded);
  EXPECT_TRUE(removed);
}


TEST_F(QPACKHeaderTableTests, decoderRemoveBetweenDecodeAndAdd) {
  // decode, delete, add
  bool decoded = false;
  table.decodeIndexRef(1)
    .then([&decoded] (QPACKHeaderTable::DecodeResult result) {
        EXPECT_EQ(result.which, 0);
        EXPECT_EQ(result.ref, HPACKHeader("accept-encoding", "br"));
        decoded = true;
      });
  EXPECT_FALSE(decoded);

  bool removed = false;
  table.decoderRemove(1, 2).
    then([&removed] { removed = true; });
  EXPECT_FALSE(removed);

  // It's not in the table after add because of pending delete
  EXPECT_FALSE(table.add(HPACKHeader("accept-encoding", "br"), 1));
  EXPECT_TRUE(decoded);
  EXPECT_TRUE(removed);
}

TEST_F(QPACKHeaderTableTests, decoderRemoveBadCount) {
  // delete, decode, add
  bool removed = false;
  table.decoderRemove(1, 1)  // bad count, should be 2
    .then([&removed] { removed = true; });
  EXPECT_FALSE(removed);

  bool decoded = false;
  bool error = false;
  table.decodeIndexRef(1)
    .then([&decoded] (QPACKHeaderTable::DecodeResult) { decoded = true; })
    .onError([&error] (const std::runtime_error&) { error = true; });
  EXPECT_FALSE(decoded);
  EXPECT_FALSE(error);

  // It's not in the table after add because of pending delete
  EXPECT_FALSE(table.add(HPACKHeader("accept-encoding", "br"), 1));
  EXPECT_FALSE(decoded);
  EXPECT_TRUE(error);
  EXPECT_TRUE(removed);
}

TEST_F(QPACKHeaderTableTests, decoderRemoveDoubleDelete) {
  // delete, delete, add
  bool removed1 = false;
  table.decoderRemove(1, 1).
    then([&removed1] { removed1 = true; });
  EXPECT_FALSE(removed1);

  bool removed2 = false;
  bool error = false;
  table.decoderRemove(1, 1)
    .then([&removed2] { removed2 = true; })
    .onError([&error] (const std::runtime_error&) { error = true; });
  EXPECT_FALSE(removed2);

  // It's not in the table after add because of pending delete
  EXPECT_FALSE(table.add(HPACKHeader("accept-encoding", "br"), 1));
  EXPECT_TRUE(error);
  EXPECT_TRUE(removed1);
  EXPECT_FALSE(removed2);
}

TEST_F(QPACKHeaderTableTests, comparison) {
  uint32_t capacity = 128;
  QPACKHeaderTable t1(capacity);
  QPACKHeaderTable t2(capacity);

  HPACKHeader h1("Content-Encoding", "gzip");
  HPACKHeader h2("Content-Encoding", "deflate");
  // different in number of elements
  t1.add(h1, 1);
  EXPECT_FALSE(t1 == t2);
  // different in size (bytes)
  t2.add(h2, 2);
  EXPECT_FALSE(t1 == t2);

  // make them the same
  t1.add(h2, 2);
  t2.add(h1, 1);
  EXPECT_TRUE(t1 == t2);
}

TEST_F(QPACKHeaderTableTests, print) {
  stringstream out;
  QPACKHeaderTable t(128);
  t.add(HPACKHeader("Accept-Encoding", "gzip"), 1);
  out << t;
  EXPECT_EQ(out.str(),
  "\n[1] (s=51) accept-encoding: gzip\ntotal size: 51\n");
}
}
