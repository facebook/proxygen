/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/Cursor.h>
#include <folly/io/IOBufQueue.h>
#include <gtest/gtest.h>
#include <memory>
#include <proxygen/lib/http/codec/compress/Huffman.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <tuple>

using namespace folly::io;
using namespace folly;
using namespace proxygen::huffman;
using namespace proxygen;
using namespace std;
using namespace testing;

class HuffmanTests : public testing::Test {
 protected:
  const HuffTree& reqTree_ = reqHuffTree05();
  const HuffTree& respTree_ = respHuffTree05();
};

TEST_F(HuffmanTests, codes) {
  uint32_t code;
  uint8_t bits;
  // check 'e' for both requests and responses
  tie(code, bits) = reqTree_.getCode('e');
  EXPECT_EQ(code, 0x01);
  EXPECT_EQ(bits, 4);
  tie(code, bits) = respTree_.getCode('e');
  EXPECT_EQ(code, 0x10);
  EXPECT_EQ(bits, 5);
  // some extreme cases
  tie(code, bits) = reqTree_.getCode(0);
  EXPECT_EQ(code, 0x7ffffba);
  EXPECT_EQ(bits, 27);
  tie(code, bits) = reqTree_.getCode(255);
  EXPECT_EQ(code, 0x3ffffdb);
  EXPECT_EQ(bits, 26);

  tie(code, bits) = respTree_.getCode(0);
  EXPECT_EQ(code, 0x1ffffbc);
  EXPECT_EQ(bits, 25);
  tie(code, bits) = respTree_.getCode(255);
  EXPECT_EQ(code, 0xffffdc);
  EXPECT_EQ(bits, 24);
}

TEST_F(HuffmanTests, size) {
  uint32_t size;
  string onebyte("/e");
  size = reqTree_.getEncodeSize(onebyte);
  EXPECT_EQ(size, 1);

  string accept("accept-encoding");
  size = reqTree_.getEncodeSize(accept);
  EXPECT_EQ(size, 10);
  size = respTree_.getEncodeSize(accept);
  EXPECT_EQ(size, 11);
}

TEST_F(HuffmanTests, encode) {
  uint32_t size;
  // this is going to fit perfectly into 3 bytes
  string gzip("gzip");
  IOBufQueue bufQueue;
  QueueAppender appender(&bufQueue, 512);
  // force the allocation
  appender.ensure(512);

  size = reqTree_.encode(gzip, appender);
  EXPECT_EQ(size, 3);
  const IOBuf* buf = bufQueue.front();
  const uint8_t* data = buf->data();
  EXPECT_EQ(data[0], 203); // 11001011
  EXPECT_EQ(data[1], 213); // 11010101
  EXPECT_EQ(data[2], 78);  // 01001110

  // size must equal with the actual encoding
  string accept("accept-encoding");
  size = reqTree_.getEncodeSize(accept);
  uint32_t encodedSize = reqTree_.encode(accept, appender);
  EXPECT_EQ(size, encodedSize);
}

TEST_F(HuffmanTests, decode) {
  uint8_t buffer[3];
  // simple test with one byte
  buffer[0] = 1; // 0000 0001
  string literal;
  reqTree_.decode(buffer, 1, literal);
  CHECK_EQ(literal, "/e");

  // simple test with "gzip"
  buffer[0] = 203;
  buffer[1] = 213;
  buffer[2] = 78;
  literal.clear();
  reqTree_.decode(buffer, 3, literal);
  EXPECT_EQ(literal, "gzip");

  // something with padding
  buffer[0] = 200;
  buffer[1] = 127;
  literal.clear();
  reqTree_.decode(buffer, 2, literal);
  EXPECT_EQ(literal, "ge");
}

/*
 * non-printable characters, that use 3 levels
 */
TEST_F(HuffmanTests, non_printable_decode) {
  // character code 1 and 46 (.) that have 27 + 5 = 32 bits
  uint8_t buffer1[4] = {
    0xFF, 0xFF, 0xF7, 0x64
  };
  string literal;
  reqTree_.decode(buffer1, 4, literal);
  EXPECT_EQ(literal.size(), 2);
  EXPECT_EQ((uint8_t)literal[0], 1);
  EXPECT_EQ((uint8_t)literal[1], 46);

  // two weird characters and padding
  // 1 and 240 will have 27 + 26 = 53 bits + 3 bits padding
  uint8_t buffer2[7] = {
    0xFF, 0xFF, 0xF7, 0x7F, 0xFF, 0xFE, 0x67
  };
  literal.clear();
  reqTree_.decode(buffer2, 7, literal);
  EXPECT_EQ(literal.size(), 2);
  EXPECT_EQ((uint8_t) literal[0], 1);
  EXPECT_EQ((uint8_t) literal[1], 240);
}

TEST_F(HuffmanTests, example_com) {
  // interesting case of one bit with value 0 in the last byte
  IOBufQueue bufQueue;
  QueueAppender appender(&bufQueue, 512);
  appender.ensure(512);

  string example("www.example.com");
  uint32_t size = reqTree_.getEncodeSize(example);
  EXPECT_EQ(size, 11);
  uint32_t encodedSize = reqTree_.encode(example, appender);
  EXPECT_EQ(size, encodedSize);

  string decoded;
  reqTree_.decode(bufQueue.front()->data(), size, decoded);
  CHECK_EQ(example, decoded);
}

TEST_F(HuffmanTests, user_agent) {
  string user_agent(
    "Mozilla/5.0 (iPhone; CPU iPhone OS 7_0_4 like Mac OS X) AppleWebKit/537.51"
    ".1 (KHTML, like Gecko) Mobile/11B554a [FBAN/FBIOS;FBAV/6.7;FBBV/566055;FBD"
    "V/iPhone5,1;FBMD/iPhone;FBSN/iPhone OS;FBSV/7.0.4;FBSS/2; FBCR/AT&T;FBID/p"
    "hone;FBLC/en_US;FBOP/5]");
  for (int i = 0; i < 2; i++) {
    IOBufQueue bufQueue;
    QueueAppender appender(&bufQueue, 512);
    appender.ensure(512);
    const HuffTree& tree = (i == 0) ? reqHuffTree05() : respHuffTree05();
    uint32_t size = tree.getEncodeSize(user_agent);
    uint32_t encodedSize = tree.encode(user_agent, appender);
    EXPECT_EQ(size, encodedSize);

    string decoded;
    tree.decode(bufQueue.front()->data(), size, decoded);
    CHECK_EQ(user_agent, decoded);
  }
}

/*
 * this test is verifying the CHECK for length at the end of huffman::encode()
 */
TEST_F(HuffmanTests, fit_in_buffer) {
  IOBufQueue bufQueue;
  QueueAppender appender(&bufQueue, 128);

  // call with an empty string
  string literal("");
  reqTree_.encode(literal, appender);

  // allow just 1 byte
  appender.ensure(128);
  appender.append(appender.length() - 1);
  literal = "g";
  reqTree_.encode(literal, appender);
  CHECK_EQ(appender.length(), 0);
}

/*
 * sanity checks of each node in decode tree performed by a depth first search
 *
 * allSnodes is an array of up to 46 SuperHuffNode's, the 46 is hardcoded
 * in creation
 * nodeIndex is the current SuperHuffNode being visited
 * depth is the depth of the current SuperHuffNode being visited
 * fullCode remembers the code from parent HuffNodes
 * eosCode stores the code for End-Of-String characters which the tables
 *   do not store
 * eosCodeBits stores the number of bits for the End-Of-String character
 *   codes
 */
uint32_t treeDfs(
    const SuperHuffNode *allSnodes,
    const uint32_t &snodeIndex,
    const uint32_t &depth,
    const uint32_t &fullCode,
    const uint32_t &eosCode,
    const uint32_t &eosCodeBits) {

  EXPECT_TRUE(depth < 4);

  unordered_set<uint32_t> leaves;
  uint32_t subtreeLeafCount = 0;

  for (uint32_t i = 0; i < 256; i++) {
    const HuffNode& node = allSnodes[snodeIndex].index[i];

    uint32_t newFullCode = fullCode ^ (i << (24 - 8 * depth));
    uint32_t eosCodeDepth = (eosCodeBits - 1) / 8;

    if(eosCodeDepth == depth
        && (newFullCode >> (32 - eosCodeBits)) == eosCode) {

      // this condition corresponds to an EOS code
      // this should be a leaf that doesn't have supernode or bits set

      EXPECT_TRUE(node.isLeaf());
      EXPECT_TRUE(node.metadata.bits == 0);
    } else if (node.isLeaf()) {

      // this condition is a normal leaf
      // this should have bits set

      EXPECT_TRUE(node.isLeaf());
      EXPECT_TRUE(node.metadata.bits > 0);
      EXPECT_TRUE(node.metadata.bits <= 8);

      // used to count unique leaves at this node
      leaves.insert(node.data.ch);
    } else {

      // this condition is a branching node
      // this should have the superNodeIndex set but not bits should be set

      EXPECT_TRUE(!node.isLeaf());
      EXPECT_TRUE(node.data.superNodeIndex > 0);
      EXPECT_TRUE(node.metadata.bits == 0);
      EXPECT_TRUE(node.data.superNodeIndex < 46);

      // keep track of leaf counts for this subtree
      subtreeLeafCount += treeDfs(
          allSnodes,
          node.data.superNodeIndex,
          depth + 1,
          newFullCode,
          eosCode,
          eosCodeBits);
    }
  }
  return subtreeLeafCount + leaves.size();
}

/**
 * Class used in testing to expose the internal tables for requests
 * and responses
 */
class TestingHuffTree : public HuffTree {
 public:

  explicit TestingHuffTree(const HuffTree& tree) : HuffTree(tree) {}

  const SuperHuffNode* getInternalTable() {
    return table_;
  }

  static TestingHuffTree getReqHuffTree() {
    TestingHuffTree reqTree(reqHuffTree05());
    return reqTree;
  }

  static TestingHuffTree getRespHuffTree() {
    TestingHuffTree respTree(respHuffTree05());
    return respTree;
  }

};

TEST_F(HuffmanTests, sanity_checks) {
  TestingHuffTree reqTree = TestingHuffTree::getReqHuffTree();
  const SuperHuffNode* allSnodesReq = reqTree.getInternalTable();
  uint32_t totalReqChars = treeDfs(allSnodesReq, 0, 0, 0, 0x3ffffdc, 26);
  EXPECT_EQ(totalReqChars, 256);

  TestingHuffTree respTree = TestingHuffTree::getRespHuffTree();
  const SuperHuffNode* allSnodesResp = respTree.getInternalTable();
  uint32_t totalRespChars = treeDfs(allSnodesResp, 0, 0, 0, 0xffffdd, 24);
  EXPECT_EQ(totalRespChars, 256);
}
