/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/experimental/RFC1867.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace testing;
using std::unique_ptr;
using std::map;
using std::list;
using std::string;
using folly::IOBuf;
using folly::IOBufQueue;

namespace {
const std::string kTestBoundary("abcdef");

unique_ptr<IOBuf> makePost(const map<string, string>& params,
                           const map<string, size_t>& files) {
  IOBufQueue result;
  for (const auto& kv: params) {
    result.append("--");
    result.append(kTestBoundary);
    result.append("\r\nContent-Disposition: form-data; name=\"");
    result.append(kv.first);
    result.append("\"\r\n\r\n");
    result.append(kv.second);
    result.append("\r\n");
  }
  for (const auto& kv: files) {
    result.append("--");
    result.append(kTestBoundary);
    result.append("\r\nContent-Disposition: form-data; filename=\"");
    result.append(kv.first);
    result.append("\"\r\n"
                  "Content-Type: text/plain\r\n"
                  "\r\n");
    result.append(proxygen::makeBuf(kv.second));
    result.append("\r\n");
  }
  result.append("--");
  result.append(kTestBoundary);

  return result.move();
}

}

namespace proxygen {

class Mock1867Callback : public RFC1867Codec::Callback {
 public:
  MOCK_METHOD3(onParam, void(const string& name, const string& value,
                            uint64_t bytesProcessed));
  MOCK_METHOD4(onFileStart, int(const string& name, const string& filename,
                                std::shared_ptr<HTTPMessage> msg,
                               uint64_t bytesProcessed));
  int onFileStart(const string& name, const string& filename,
                  std::unique_ptr<HTTPMessage> msg,
                  uint64_t bytesProcessed) override {
    std::shared_ptr<HTTPMessage> sh_msg(msg.release());
    return onFileStart(name, filename, sh_msg, bytesProcessed);
  }
  MOCK_METHOD2(onFileData, int(std::shared_ptr<folly::IOBuf>, uint64_t));
  int onFileData(std::unique_ptr<folly::IOBuf> data,
                 uint64_t bytesProcessed) override {
    std::shared_ptr<IOBuf> sh_data(data.release());
    return onFileData(sh_data, bytesProcessed);
  }

  MOCK_METHOD2(onFileEnd, void(bool, uint64_t));
  MOCK_METHOD0(onError, void());
};

class RFC1867Test : public testing::Test {
 public:

  void SetUp() override {
    codec_.setCallback(&callback_);
  }

  void parse(unique_ptr<IOBuf> input, size_t chunkSize = 0) {
    IOBufQueue ibuf{IOBufQueue::cacheChainLength()};
    ibuf.append(std::move(input));
    if (chunkSize == 0) {
      chunkSize = ibuf.chainLength();
    }
    unique_ptr<IOBuf> rem;
    while (!ibuf.empty()) {
      auto chunk = ibuf.split(std::min(chunkSize, ibuf.chainLength()));
      if (rem) {
        rem->prependChain(std::move(chunk));
        chunk = std::move(rem);
      }
      rem = codec_.onIngress(std::move(chunk));
    }
    codec_.onIngressEOM();
  }

 protected:
  void testSimple(size_t fileSize, size_t splitSize);

  StrictMock<Mock1867Callback> callback_;
  RFC1867Codec codec_{kTestBoundary};

};

void RFC1867Test::testSimple(size_t fileSize, size_t splitSize) {
  auto data = makePost({{"foo", "bar"}, {"jojo", "binky"}},
                       {{"file1", fileSize}});
  size_t fileLength = 0;
  EXPECT_CALL(callback_, onParam(string("foo"), string("bar"), _));
  EXPECT_CALL(callback_, onParam(string("jojo"), string("binky"), _));
  EXPECT_CALL(callback_, onFileStart(_, _, _, _))
    .WillOnce(Return(0));
  EXPECT_CALL(callback_, onFileData(_, _))
    .WillRepeatedly(Invoke([&] (std::shared_ptr<IOBuf> data, uint64_t) {
          fileLength += data->computeChainDataLength();
          return 0;
        }));
  EXPECT_CALL(callback_, onFileEnd(true, _));
  parse(std::move(data), splitSize);
  ASSERT_EQ(fileLength, fileSize);
}

TEST_F(RFC1867Test, testSimplePost) {
  testSimple(17, 0);
}

TEST_F(RFC1867Test, testSplits) {
  for (size_t i = 1; i < 500; i++) {
    testSimple(1000 + i, i);
  }
}


}
