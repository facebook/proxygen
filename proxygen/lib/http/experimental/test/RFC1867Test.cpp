/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/experimental/RFC1867.h>
#include <proxygen/lib/http/codec/test/TestUtils.h>

#include <folly/portability/GTest.h>
#include <folly/portability/GMock.h>

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
                           const map<string, string>& explicitFiles,
                           const map<string, size_t>& randomFiles) {
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
  for (const auto& kv: explicitFiles) {
    result.append("--");
    result.append(kTestBoundary);
    result.append("\r\nContent-Disposition: form-data; filename=\"");
    result.append(kv.first);
    result.append("\"\r\n"
                  "Content-Type: text/plain\r\n"
                  "\r\n");
    result.append(IOBuf::copyBuffer(kv.second.data(), kv.second.length()));
    result.append("\r\n");
  }
  for (const auto& kv: randomFiles) {
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

class RFC1867Base {
 public:

  void SetUp() {
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
  void testSimple(unique_ptr<IOBuf> data, size_t fileSize, size_t splitSize);

  StrictMock<Mock1867Callback> callback_;
  RFC1867Codec codec_{kTestBoundary};

};

class RFC1867Test : public testing::Test, public RFC1867Base {
 public:
  void SetUp() override {
    RFC1867Base::SetUp();
  }
};

void RFC1867Base::testSimple(unique_ptr<IOBuf> data, size_t fileSize,
                             size_t splitSize) {
  size_t fileLength = 0;
  IOBufQueue parsedData{IOBufQueue::cacheChainLength()};
  EXPECT_CALL(callback_, onParam(string("foo"), string("bar"), _));
  EXPECT_CALL(callback_, onParam(string("jojo"), string("binky"), _));
  EXPECT_CALL(callback_, onFileStart(_, _, _, _))
    .WillOnce(Return(0));
  EXPECT_CALL(callback_, onFileData(_, _))
    .WillRepeatedly(Invoke([&] (std::shared_ptr<IOBuf> data, uint64_t) {
          fileLength += data->computeChainDataLength();
          parsedData.append(data->clone());
          return 0;
        }));
  EXPECT_CALL(callback_, onFileEnd(true, _));
  parse(data->clone(), splitSize);
  auto parsedDataBuf = parsedData.move();
  parsedDataBuf->coalesce();
  CHECK_EQ(fileLength, fileSize);
}

TEST_F(RFC1867Test, testSimplePost) {
  size_t fileSize = 17;
  auto data = makePost({{"foo", "bar"}, {"jojo", "binky"}},
                       {}, {{"file1", fileSize}});
  testSimple(std::move(data), fileSize, 0);
}

TEST_F(RFC1867Test, testSplits) {
  for (size_t i = 1; i < 500; i++) {
    size_t fileSize = 1000 + i;
    auto data = makePost({{"foo", "bar"}, {"jojo", "binky"}},
                         {}, {{"file1", fileSize}});
    testSimple(std::move(data), fileSize, i);
  }
}

class RFC1867CR : public testing::TestWithParam<string>, public RFC1867Base {
 public:
  void SetUp() override {
    RFC1867Base::SetUp();
  }
};


TEST_P(RFC1867CR, test) {
  for (size_t i = 1; i < GetParam().size(); i++) {
    auto data = makePost({{"foo", "bar"}, {"jojo", "binky"}},
                         {{"file1", GetParam()}}, {});
    testSimple(std::move(data), GetParam().size(), i);
  }
}

INSTANTIATE_TEST_CASE_P(
  ValueTest,
  RFC1867CR,
  ::testing::Values(
    // embedded \r\n
    string("zyx\r\nwvu", 8),
    // leading \r
    string("\rzyxwvut", 8),
    // trailing \r
    string("zyxwvut\r", 8),
    // leading \n
    string("\nzyxwvut", 8),
    // trailing \n
    string("zyxwvut\n", 8),
    // all \r\n
    string("\r\n\r\n\r\n\r\n", 8),
    // all \r
    string("\r\r\r\r\r\r\r\r", 8)
  ));



}
