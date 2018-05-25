/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/portability/GTest.h>
#include <memory>
#include <proxygen/lib/http/codec/compress/QPACKHeaderTable.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <sstream>

using namespace std;
using namespace testing;

namespace proxygen {

class QPACKHeaderTableTests : public testing::Test {
 public:

 protected:
  QPACKHeaderTable table_{320, true, 15};
};

TEST_F(QPACKHeaderTableTests, indexing) {
  HPACKHeader accept("accept-encoding", "gzip");
  HPACKHeader agent("user-agent", "SeaMonkey");

  EXPECT_EQ(table_.getBaseIndex(), 0);
  table_.add(accept);
  EXPECT_EQ(table_.getBaseIndex(), 1);
  // Vulnerable - in the table
  EXPECT_EQ(table_.getIndex(accept, false),
            std::numeric_limits<uint32_t>::max());
  // Allow vulnerable, get the index
  EXPECT_EQ(table_.getIndex(accept, true), 1);
  table_.setMaxAcked(1);
  EXPECT_EQ(table_.getIndex(accept, false), 1);
  table_.add(agent);
  // Indexes move
  EXPECT_EQ(table_.getIndex(agent, true), 1);
  EXPECT_EQ(table_.getIndex(accept, true), 2);
}

TEST_F(QPACKHeaderTableTests, eviction) {
  HPACKHeader accept("accept-encoding", "gzip");

  int32_t max = 4;
  uint32_t capacity = accept.bytes() * max;
  table_.setCapacity(capacity);

  for (auto i = 0; i < max; i++) {
    EXPECT_TRUE(table_.add(accept));
  }
  for (auto i = 1; i <= max; i++) {
    table_.addRef(i);
  }
  EXPECT_FALSE(table_.canIndex(accept));
  EXPECT_FALSE(table_.add(accept));
  table_.subRef(1);
  EXPECT_TRUE(table_.canIndex(accept));
  EXPECT_TRUE(table_.add(accept));

  table_.subRef(3);
  EXPECT_FALSE(table_.canIndex(accept));
  table_.subRef(2);
  EXPECT_TRUE(table_.canIndex(accept));
}

TEST_F(QPACKHeaderTableTests, wrapcount) {
  HPACKHeader accept("accept-encoding", "gzip");
  HPACKHeader agent("user-agent", "SeaMonkey");
  HPACKHeader cookie("Cookie", "choco=chip");

  for (auto i = 0; i < 10; i++) {
    EXPECT_TRUE(table_.add(accept));
  }
  EXPECT_TRUE(table_.add(cookie));
  EXPECT_TRUE(table_.add(agent));

  EXPECT_EQ(table_.getBaseIndex(), 12);
  EXPECT_EQ(table_.getIndex(agent, true), 1);
  EXPECT_EQ(table_.getIndex(cookie, true), 2);
  EXPECT_EQ(table_.getIndex(accept, true), 3);
  EXPECT_EQ(table_.getHeader(1, table_.getBaseIndex()), agent);
  EXPECT_EQ(table_.getHeader(2, table_.getBaseIndex()), cookie);
  EXPECT_EQ(table_.getHeader(table_.size(), table_.getBaseIndex()), accept);
}

TEST_F(QPACKHeaderTableTests, name_index) {
  HPACKHeader accept("accept-encoding", "gzip");
  EXPECT_EQ(table_.nameIndex(accept.name), 0);
  EXPECT_TRUE(table_.add(accept));
  EXPECT_EQ(table_.nameIndex(accept.name), 1);
}

TEST_F(QPACKHeaderTableTests, get_index) {
  HPACKHeader accept1("accept-encoding", "gzip");
  HPACKHeader accept2("accept-encoding", "blarf");
  EXPECT_EQ(table_.getIndex(accept1), 0);
  EXPECT_TRUE(table_.add(accept1));
  EXPECT_EQ(table_.getIndex(accept1), 1);
  EXPECT_EQ(table_.getIndex(accept2), 0);
}

TEST_F(QPACKHeaderTableTests, duplication) {
  HPACKHeader accept("accept-encoding", "gzip");

  EXPECT_TRUE(table_.add(accept));

  // Unnecessary duplicate
  auto res = table_.maybeDuplicate(1, true);
  EXPECT_FALSE(res.first);
  EXPECT_EQ(res.second, 1);

  for (auto i = 0; i < 6; i++) {
    EXPECT_TRUE(table_.add(accept));
  }

  // successful duplicate, vulnerable allowed
  EXPECT_TRUE(table_.isDraining(table_.size()));
  res = table_.maybeDuplicate(table_.size(), true);
  EXPECT_TRUE(res.first);
  EXPECT_EQ(res.second, 8);
  EXPECT_EQ(table_.size(), 6); // evicted 1

  // successful duplicate, vulnerable disallowed
  table_.setMaxAcked(3);
  res = table_.maybeDuplicate(table_.size(), false);
  EXPECT_TRUE(res.first);
  EXPECT_EQ(res.second, 0);
  EXPECT_EQ(table_.size(), 6); // evicted 2

  // Attempt to duplicate UNACKED
  res = table_.maybeDuplicate(QPACKHeaderTable::UNACKED, true);
  EXPECT_FALSE(res.first);
  EXPECT_EQ(res.second, 0);
  EXPECT_EQ(table_.size(), 6); // nothing changed
  EXPECT_EQ(table_.getBaseIndex(), 9);

  // Hold a ref to oldest entry, prevents eviction
  auto oldestAbsolute = table_.getBaseIndex() - table_.size() + 1;
  table_.addRef(oldestAbsolute);

  // Table should be full
  EXPECT_FALSE(table_.canIndex(accept));

  res = table_.maybeDuplicate(table_.size(), true);
  EXPECT_FALSE(res.first);
  EXPECT_EQ(res.second, 0);

}

}
