/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/IOBuf.h>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <list>
#include <memory>
#include <proxygen/lib/http/codec/compress/HPACKEncodeBuffer.h>
#include <proxygen/lib/http/codec/compress/HPACKHeader.h>
#include <proxygen/lib/http/codec/compress/Logging.h>
#include <sstream>
#include <vector>

using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;

class LoggingTests : public testing::Test {
};

TEST_F(LoggingTests, hex_iobuf) {
  unique_ptr<IOBuf> buf = IOBuf::create(128);
  stringstream out;
  out << buf.get();
  EXPECT_EQ(out.str(), "");

  uint8_t* data = buf->writableData();
  data[0] = 0x0C;
  data[1] = 0xFF;
  data[2] = 0x00;
  data[3] = 0x10;
  buf->append(4);
  out << buf.get();
  EXPECT_EQ(out.str(), "0cff 0010 ");

  // some linewrap
  for (int i = 0; i < 16; i++) {
    data[4 + i] = 0xFE;
  }
  buf->append(16);
  out.str("");
  out << buf.get();
  EXPECT_EQ(out.str(), "0cff 0010 fefe fefe fefe fefe fefe fefe \nfefe fefe ");
}

TEST_F(LoggingTests, refset) {
  list<uint32_t> refset;
  refset.push_back(3);
  refset.push_back(5);
  stringstream out;
  out << &refset;
  EXPECT_EQ(out.str(), "\n[3 5 ]\n");
}

TEST_F(LoggingTests, dump_header_vector) {
  vector<HPACKHeader> headers;
  headers.push_back(HPACKHeader(":path", "index.html"));
  headers.push_back(HPACKHeader("content-type", "gzip"));
  stringstream out;
  out << headers;
  EXPECT_EQ(out.str(), ":path: index.html\ncontent-type: gzip\n\n");
}

TEST_F(LoggingTests, dump_chain) {
  unique_ptr<IOBuf> b1 = IOBuf::create(128);
  EXPECT_TRUE(dumpChain(b1.get()) != "");
}

TEST_F(LoggingTests, dump_bin) {
  // null IOBuf
  EXPECT_EQ(dumpBin(nullptr, 1), "");

  unique_ptr<IOBuf> b1 = IOBuf::create(128);
  b1->writableData()[0] = 0x33;
  b1->writableData()[1] = 0x77;
  b1->append(2);
  unique_ptr<IOBuf> b2 = IOBuf::create(128);
  b2->writableData()[0] = 0xFF;
  b1->appendChain(std::move(b2));
  EXPECT_EQ(dumpBin(b1.get(), 1), "00110011 3\n01110111 w\n\n\n");
  EXPECT_EQ(dumpBin(b1.get(), 2), "00110011 3 01110111 w\n\n\n");
  // test with an HPACKEncodeBuffer
  HPACKEncodeBuffer buf(128);
  buf.encodeLiteral("test");
  EXPECT_EQ(buf.toBin(),
            "00000100   01110100 t 01100101 e 01110011 s 01110100 t \n");
}

TEST_F(LoggingTests, print_delta) {
  vector<HPACKHeader> v1;
  v1.push_back(HPACKHeader(":path", "/"));
  v1.push_back(HPACKHeader(":host", "www.facebook.com"));
  vector<HPACKHeader> v2;

  // empty v1 or v2
  EXPECT_EQ(printDelta(v1, v2), "\n + :path: /\n + :host: www.facebook.com\n");
  EXPECT_EQ(printDelta(v2, v1), "\n - :path: /\n - :host: www.facebook.com\n");

  // skip first header from v1
  v2.push_back(HPACKHeader(":path", "/"));
  EXPECT_EQ(printDelta(v1, v2), "\n + :host: www.facebook.com\n");

  v2.push_back(HPACKHeader(":path", "/"));
  EXPECT_EQ(printDelta(v2, v1),
            "\n - :host: www.facebook.com\n + :path: / (duplicate)\n");

  v2.pop_back();
  v1.clear();
  v1.push_back(HPACKHeader(":a", "b"));
  v1.push_back(HPACKHeader(":a", "b"));
  v1.push_back(HPACKHeader(":path", "/"));
  EXPECT_EQ(printDelta(v1, v2), "\n + :a: b\n duplicate :a: b\n");
}

TEST_F(LoggingTests, dump_bin_to_file) {
  struct stat fstat;
  string tmpfile("/tmp/test.bin");

  unlink(tmpfile.c_str());
  unique_ptr<IOBuf> buf = IOBuf::create(128);
  // the content doesn't matter
  buf->append(2);
  dumpBinToFile(tmpfile, buf.get());
  EXPECT_EQ(stat(tmpfile.c_str(), &fstat), 0);

  // check if it's going to overwrite the existing file
  buf->append(4);
  dumpBinToFile(tmpfile, buf.get());
  EXPECT_EQ(stat(tmpfile.c_str(), &fstat), 0);
  EXPECT_EQ(fstat.st_size, 2);
  unlink(tmpfile.c_str());

  // null iobuf
  dumpBinToFile(tmpfile, nullptr);
  // unable to open file
  dumpBinToFile("/proc/test", nullptr);
}
