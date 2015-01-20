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
#include <proxygen/lib/utils/Logging.h>

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
