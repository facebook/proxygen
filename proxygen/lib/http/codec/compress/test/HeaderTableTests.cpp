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
#include <proxygen/lib/http/codec/compress/HeaderTable.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <sstream>

using namespace std;
using namespace testing;

namespace proxygen {

class HeaderTableTests : public testing::Test {
 protected:
  void xcheck(uint32_t internal, uint32_t external) {
    EXPECT_EQ(HeaderTable::toExternal(head_, length_, internal), external);
    EXPECT_EQ(HeaderTable::toInternal(head_, length_, external), internal);
  }

  uint32_t head_{0};
  uint32_t length_{0};
};

TEST_F(HeaderTableTests, index_translation) {
  // simple cases
  length_ = 10;
  head_ = 5;
  xcheck(0, 6);
  xcheck(3, 3);
  xcheck(5, 1);

  // wrap
  head_ = 1;
  xcheck(0, 2);
  xcheck(8, 4);
  xcheck(5, 7);
}

TEST_F(HeaderTableTests, add) {
  HeaderTable table(4096);
  table.add(HPACKHeader("accept-encoding", "gzip"));
  table.add(HPACKHeader("accept-encoding", "gzip"));
  table.add(HPACKHeader("accept-encoding", "gzip"));
  EXPECT_EQ(table.names().size(), 1);
  EXPECT_EQ(table.hasName("accept-encoding"), true);
  auto it = table.names().find("accept-encoding");
  EXPECT_EQ(it->second.size(), 3);
  EXPECT_EQ(table.nameIndex("accept-encoding"), 1);
}

TEST_F(HeaderTableTests, evict) {
  HPACKHeader accept("accept-encoding", "gzip");
  HPACKHeader accept2("accept-encoding", "----"); // same size, different header
  HPACKHeader accept3("accept-encoding", "third"); // size is larger with 1 byte
  uint32_t max = 10;
  uint32_t capacity = accept.bytes() * max;
  HeaderTable table(capacity);
  // fill the table
  for (size_t i = 0; i < max; i++) {
    EXPECT_EQ(table.add(accept), true);
  }
  EXPECT_EQ(table.size(), max);
  EXPECT_EQ(table.add(accept2), true);
  // evict the first one
  EXPECT_EQ(table[1], accept2);
  auto ilist = table.names().find("accept-encoding")->second;
  EXPECT_EQ(ilist.size(), max);
  // evict all the 'accept' headers
  for (size_t i = 0; i < max - 1; i++) {
    EXPECT_EQ(table.add(accept2), true);
  }
  EXPECT_EQ(table.size(), max);
  EXPECT_EQ(table[max], accept2);
  EXPECT_EQ(table.names().size(), 1);
  // add an entry that will cause 2 evictions
  EXPECT_EQ(table.add(accept3), true);
  EXPECT_EQ(table[1], accept3);
  EXPECT_EQ(table.size(), max - 1);

  // add a super huge header
  string bigvalue;
  bigvalue.append(capacity, 'x');
  HPACKHeader bigheader("user-agent", bigvalue);
  EXPECT_EQ(table.add(bigheader), false);
  EXPECT_EQ(table.size(), 0);
  EXPECT_EQ(table.names().size(), 0);
}

TEST_F(HeaderTableTests, set_capacity) {
  HPACKHeader accept("accept-encoding", "gzip");
  uint32_t max = 10;
  uint32_t capacity = accept.bytes() * max;
  HeaderTable table(capacity);

  // fill the table
  for (size_t i = 0; i < max; i++) {
    EXPECT_EQ(table.add(accept), true);
  }
  // change capacity
  table.setCapacity(capacity / 2);
  EXPECT_EQ(table.size(), max / 2);
  EXPECT_EQ(table.bytes(), capacity / 2);
}

TEST_F(HeaderTableTests, comparison) {
  uint32_t capacity = 128;
  HeaderTable t1(capacity);
  HeaderTable t2(capacity);

  HPACKHeader h1("Content-Encoding", "gzip");
  HPACKHeader h2("Content-Encoding", "deflate");
  // different in number of elements
  t1.add(h1);
  EXPECT_FALSE(t1 == t2);
  // different in size (bytes)
  t2.add(h2);
  EXPECT_FALSE(t1 == t2);

  // make them the same
  t1.add(h2);
  t2.add(h1);
  EXPECT_TRUE(t1 == t2);

  // make them mismatch on refset
  t1.addReference(1);
  EXPECT_FALSE(t1 == t2);
}

TEST_F(HeaderTableTests, print) {
  stringstream out;
  HeaderTable t(128);
  t.add(HPACKHeader("Accept-Encoding", "gzip"));
  t.addReference(1);
  out << t;
  EXPECT_EQ(out.str(),
  "\n[1] (s=51) Accept-Encoding: gzip\nreference set: [1, ]\ntotal size: 51\n");
}

TEST_F(HeaderTableTests, increaseCapacity) {
  HPACKHeader accept("accept-encoding", "gzip");
  uint32_t max = 4;
  uint32_t capacity = accept.bytes() * max;
  HeaderTable table(capacity);
  EXPECT_GT(table.length(), max);

  // fill the table
  for (size_t i = 0; i < table.length() + 1; i++) {
    EXPECT_EQ(table.add(accept), true);
  }
  EXPECT_EQ(table.size(), max);
  EXPECT_EQ(table.getIndex(accept), 4);
  // head should be 0, tail should be 2
  max = 8;
  table.setCapacity(accept.bytes() * max);

  EXPECT_GT(table.length(), max);
  // external index didn't change
  EXPECT_EQ(table.getIndex(accept), 4);

}

}
