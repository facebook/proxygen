/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/io/IOBuf.h>
#include <glog/logging.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/utils/Logging.h>

using namespace folly;
using namespace proxygen;
using namespace std;
using namespace testing;

class LoggingTests : public testing::Test {
};

TEST_F(LoggingTests, PrintHexIobuf) {
  unique_ptr<IOBuf> buf = IOBuf::create(128);
  EXPECT_EQ(IOBufPrinter::printHexFolly(buf.get()), "");
  EXPECT_EQ(IOBufPrinter::printHex16(buf.get()), "");

  uint8_t* data = buf->writableData();
  data[0] = 0x0C;
  data[1] = 0xFF;
  data[2] = 0x00;
  data[3] = 0x10;
  buf->append(4);
  EXPECT_TRUE(IOBufPrinter::printHexFolly(buf.get()) != "");
  EXPECT_EQ(IOBufPrinter::printHex16(buf.get()), "0cff 0010 ");

  // some linewrap
  for (int i = 0; i < 16; i++) {
    data[4 + i] = 0xFE;
  }
  buf->append(16);
  EXPECT_TRUE(IOBufPrinter::printHexFolly(buf.get()) != "");
  string info = IOBufPrinter::printChainInfo(buf.get());
  EXPECT_TRUE(info.find("iobuf of size 20 tailroom ") != string::npos);
  EXPECT_EQ(IOBufPrinter::printHex16(buf.get()),
            "0cff 0010 fefe fefe fefe fefe fefe fefe \nfefe fefe ");
}

TEST_F(LoggingTests, HexString) {
  uint8_t buf[] = {
    0x03, 0x04, 0x11, 0x22, 0xBB, 0xAA
  };
  string s((const char *)buf, sizeof(buf));
  EXPECT_EQ("03041122bbaa", hexStr(s));
}

TEST_F(LoggingTests, DumpBin) {
  // null IOBuf
  EXPECT_EQ(IOBufPrinter::printBin(nullptr), "");

  unique_ptr<IOBuf> b1 = IOBuf::create(128);
  b1->writableData()[0] = 0x33;
  b1->writableData()[1] = 0x77;
  b1->append(2);
  unique_ptr<IOBuf> b2 = IOBuf::create(128);
  b2->writableData()[0] = 0xFF;
  b2->append(1);
  b1->appendChain(std::move(b2));
  EXPECT_EQ(IOBufPrinter::printBin(b1.get()),
            "00110011 3 01110111 w \n11111111   \n");
  // with coalescing
  EXPECT_EQ(IOBufPrinter::printBin(b1.get(), true),
            "00110011 3 01110111 w 11111111   \n");
}

TEST_F(LoggingTests, DumpBinToFile) {
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

TEST_F(LoggingTests, CHECK_LOG_AND_THROW_TEST) {
  EXPECT_THROW([]{
    CHECK_LOG_AND_THROW(false, ERROR, runtime_error)
      << "Test log and exception message";
  }(), runtime_error);
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW(true, ERROR, runtime_error)
      << "This should not be logged";
  });
}

TEST_F(LoggingTests, CHECK_LOG_AND_THROW_LT_TEST) {
  EXPECT_THROW([]{
    CHECK_LOG_AND_THROW_LT(5, 4, ERROR, runtime_error)
      << "Test log and exception message";
  }(), runtime_error);
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW_LT(3, 4, ERROR, runtime_error)
      << "This should not be logged";
  });
}

TEST_F(LoggingTests, CHECK_LOG_AND_THROW_LE_TEST) {
  EXPECT_THROW([]{
    CHECK_LOG_AND_THROW_LE(5, 4, ERROR, runtime_error)
      << "Test log and exception message";
  }(), runtime_error);
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW_LE(3, 4, ERROR, runtime_error)
      << "This should not be logged";
  });
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW_LE(4, 4, ERROR, runtime_error)
      << "This should not be logged";
  });
}

TEST_F(LoggingTests, CHECK_LOG_AND_THROW_GT_TEST) {
  EXPECT_THROW([]{
    CHECK_LOG_AND_THROW_GT(3, 4, ERROR, runtime_error)
      << "Test log and exception message";
  }(), runtime_error);
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW_GT(5, 4, ERROR, runtime_error)
      << "This should not be logged";
  });
}

TEST_F(LoggingTests, CHECK_LOG_AND_THROW_GE_TEST) {
  EXPECT_THROW([]{
    CHECK_LOG_AND_THROW_GE(3, 4, ERROR, runtime_error)
      << "Test log and exception message";
  }(), runtime_error);
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW_GE(5, 4, ERROR, runtime_error)
      << "This should not be logged";
  });
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW_GE(4, 4, ERROR, runtime_error)
      << "This should not be logged";
  });
}

TEST_F(LoggingTests, CHECK_LOG_AND_THROW_EQ_TEST) {
  EXPECT_THROW([]{
    CHECK_LOG_AND_THROW_EQ(5, 4, ERROR, runtime_error)
      << "Test log and exception message";
  }(), runtime_error);
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW_EQ(4, 4, ERROR, runtime_error)
      << "This should not be logged";
  });
}

TEST_F(LoggingTests, CHECK_LOG_AND_THROW_NE_TEST) {
  EXPECT_THROW([]{
    CHECK_LOG_AND_THROW_NE(4, 4, ERROR, runtime_error)
      << "Test log and exception message";
  }(), runtime_error);
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW_NE(3, 4, ERROR, runtime_error)
      << "This should not be logged";
  });
}

TEST_F(LoggingTests, CHECK_LOG_AND_THROW_NOT_NULL_TEST) {
  EXPECT_THROW([]{
    CHECK_LOG_AND_THROW_NOT_NULL(nullptr, ERROR, runtime_error)
      << "Test log and exception message";
  }(), runtime_error);
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW_NOT_NULL("hello", ERROR, runtime_error)
      << "This should not be logged";
  });
}

TEST_F(LoggingTests, CHECK_LOG_AND_THROW_NULL_TEST) {
  EXPECT_THROW([]{
    CHECK_LOG_AND_THROW_NULL("hello", ERROR, runtime_error)
      << "Test log and exception message";
  }(), runtime_error);
  EXPECT_NO_THROW([]{
    CHECK_LOG_AND_THROW_NULL(nullptr, ERROR, runtime_error)
      << "This should not be logged";
  });
}
