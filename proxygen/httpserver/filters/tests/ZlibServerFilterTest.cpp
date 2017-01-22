/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Conv.h>
#include <folly/ScopeGuard.h>
#include <folly/io/IOBuf.h>

#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>
#include <proxygen/httpserver/filters/ZlibServerFilter.h>
#include <proxygen/lib/utils/ZlibStreamCompressor.h>
#include <proxygen/httpserver/Mocks.h>
#include <proxygen/httpserver/ResponseBuilder.h>

using namespace proxygen;
using namespace testing;

MATCHER_P(IOBufEquals,
          expected,
          folly::to<std::string>(
              "IOBuf is ", negation ? "not " : "", "'", expected, "'")) {
  auto iob = arg->clone();
  auto br = iob->coalesce();
  std::string actual(br.begin(), br.end());
  *result_listener << "'" << actual << "'";
  return actual == expected;
}

class ZlibServerFilterTest : public Test {
 public:
  void SetUp() override {
    // requesthandler is the server, responsehandler is the client
    requestHandler_ = new MockRequestHandler();
    responseHandler_ = folly::make_unique<MockResponseHandler>(requestHandler_);
    zd_ = folly::make_unique<ZlibStreamDecompressor>(ZlibCompressionType::GZIP);
  }

  void TearDown() override {
    Mock::VerifyAndClear(requestHandler_);
    Mock::VerifyAndClear(responseHandler_.get());

    delete requestHandler_;
  }

 protected:
  ZlibServerFilter* filter_{nullptr};
  MockRequestHandler* requestHandler_;
  std::unique_ptr<MockResponseHandler> responseHandler_;
  std::unique_ptr<ZlibStreamDecompressor> zd_;
  ResponseHandler* downstream_{nullptr};

  void exercise_compression(bool expectCompression,
                            std::string url,
                            std::string acceptedEncoding,
                            std::string expectedEncoding,
                            std::string originalRequestBody,
                            std::string responseContentType,
                            std::unique_ptr<folly::IOBuf> originalResponseBody,
                            int32_t compressionLevel = 4,
                            uint32_t minimumCompressionSize = 1) {

    // If there is only one IOBuf, then it's not chunked.
    bool isResponseChunked = originalResponseBody->isChained();
    size_t chunkCount = originalResponseBody->countChainElements();

    // Chunked and compressed responses will have an extra block
    if (isResponseChunked && expectCompression) {
      chunkCount += 1;
    }

    // Request Handler Expectations
    EXPECT_CALL(*requestHandler_, onBody(_)).Times(1);
    EXPECT_CALL(*requestHandler_, onEOM()).Times(1);

    // Need to capture whatever the filter is for ResponseBuilder later
    EXPECT_CALL(*requestHandler_, setResponseHandler(_))
        .WillOnce(DoAll(SaveArg<0>(&downstream_), Return()));

    // Response Handler Expectations
    // Headers are only sent once
    EXPECT_CALL(*responseHandler_, sendHeaders(_)).WillOnce(DoAll(
        Invoke([&](HTTPMessage& msg) {
          auto& headers = msg.getHeaders();
          if (expectCompression) {
            EXPECT_TRUE(msg.checkForHeaderToken(
                HTTP_HEADER_CONTENT_ENCODING, expectedEncoding.c_str(), false));
          }

          if (msg.getIsChunked()) {
            EXPECT_FALSE(headers.exists("Content-Length"));
          } else {
            //Content-Length is not set on chunked messages
            EXPECT_TRUE(headers.exists("Content-Length"));
          }
        }),
        Return()));

    if (isResponseChunked) {
      // The final chunk has 0 body
      EXPECT_CALL(*responseHandler_, sendChunkHeader(_)).Times(chunkCount);
      EXPECT_CALL(*responseHandler_, sendChunkTerminator()).Times(chunkCount);
    } else {
      EXPECT_CALL(*responseHandler_, sendChunkHeader(_)).Times(0);
      EXPECT_CALL(*responseHandler_, sendChunkTerminator()).Times(0);
    }

    // Accumulate the body, decompressing it if it's compressed
    std::unique_ptr<folly::IOBuf> responseBody;
    EXPECT_CALL(*responseHandler_, sendBody(_))
        .Times(chunkCount)
        .WillRepeatedly(DoAll(
            Invoke([&](std::shared_ptr<folly::IOBuf> body) {

              std::unique_ptr<folly::IOBuf> processedBody;

              if (expectCompression) {
                processedBody = zd_->decompress(body.get());
                ASSERT_FALSE(zd_->hasError())
                    << "Failed to decompress body. r=" << zd_->getStatus();
              } else {
                processedBody = folly::IOBuf::copyBuffer(
                    body->data(), body->length(), 0, 0);
              }

              if (responseBody) {
                responseBody->prependChain(std::move(processedBody));
              } else {
                responseBody = std::move(processedBody);
              }
            }),
            Return()));

    EXPECT_CALL(*responseHandler_, sendEOM()).Times(1);

    /* Simulate Request/Response  */

    HTTPMessage msg;
    msg.setURL(url);
    msg.getHeaders().set(HTTP_HEADER_ACCEPT_ENCODING, acceptedEncoding);

    std::set<std::string> compressibleTypes = {"text/html"};
    auto filterFactory = folly::make_unique<ZlibServerFilterFactory>(
        compressionLevel, minimumCompressionSize, compressibleTypes);

    auto filter = filterFactory->onRequest(requestHandler_, &msg);
    filter->setResponseHandler(responseHandler_.get());

    // Send fake request
    filter->onBody(folly::IOBuf::copyBuffer(originalRequestBody));
    filter->onEOM();

    // Send a fake Response
    if (isResponseChunked) {

      ResponseBuilder(downstream_)
        .status(200, "OK")
        .header(HTTP_HEADER_CONTENT_TYPE, responseContentType)
        .send();

      folly::IOBuf* crtBuf;
      crtBuf = originalResponseBody.get();

      do {
        ResponseBuilder(downstream_).body(crtBuf->cloneOne()).send();
        crtBuf = crtBuf->next();
      } while (crtBuf != originalResponseBody.get());

      ResponseBuilder(downstream_).sendWithEOM();

    } else {

      // Send unchunked response
      ResponseBuilder(downstream_)
          .status(200, "OK")
          .header(HTTP_HEADER_CONTENT_TYPE, responseContentType)
          .body(originalResponseBody->clone())
          .sendWithEOM();
    }

    filter->requestComplete();

    EXPECT_THAT(responseBody, IOBufEquals(originalRequestBody));
  }

  // Helper method to convert a vector of strings to an IOBuf chain
  // specificaly create a chain because the chain pieces are chunks
  std::unique_ptr<folly::IOBuf> createResponseChain(
      std::vector<std::string> const& bodyStrings) {

    std::unique_ptr<folly::IOBuf> responseBodyChain;

    for (auto& s : bodyStrings) {
      auto nextBody = folly::IOBuf::copyBuffer(s.c_str());
      if (responseBodyChain) {
        responseBodyChain->prependChain(std::move(nextBody));
      } else {
        responseBodyChain = std::move(nextBody);
      }
    }

    return responseBodyChain;
  }
};

// Basic smoke test
TEST_F(ZlibServerFilterTest, nonchunked_compression) {
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(true,
                         std::string("http://locahost/foo.compressme"),
                         std::string("gzip"),
                         std::string("gzip"),
                         std::string("Hello World"),
                         std::string("text/html"),
                         folly::IOBuf::copyBuffer("Hello World"));
  });
}

TEST_F(ZlibServerFilterTest, chunked_compression) {
  std::vector<std::string> chunks = {"Hello", " World"};
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(true,
                         std::string("http://locahost/foo.compressme"),
                         std::string("gzip"),
                         std::string("gzip"),
                         std::string("Hello World"),
                         std::string("text/html"),
                         createResponseChain(chunks));
  });
}

TEST_F(ZlibServerFilterTest, parameterized_contenttype) {
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(true,
                         std::string("http://locahost/foo.compressme"),
                         std::string("gzip"),
                         std::string("gzip"),
                         std::string("Hello World"),
                         std::string("text/html; param1"),
                         folly::IOBuf::copyBuffer("Hello World"));
  });
}

TEST_F(ZlibServerFilterTest, mixedcase_contenttype) {
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(true,
                         std::string("http://locahost/foo.compressme"),
                         std::string("gzip"),
                         std::string("gzip"),
                         std::string("Hello World"),
                         std::string("Text/Html; param1"),
                         folly::IOBuf::copyBuffer("Hello World"));
  });
}

// Client supports multiple possible compression encodings
TEST_F(ZlibServerFilterTest, multiple_accepted_encodings) {
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(true,
                         std::string("http://locahost/foo.compressme"),
                         std::string("gzip, identity, deflate"),
                         std::string("gzip"),
                         std::string("Hello World"),
                         std::string("text/html"),
                         folly::IOBuf::copyBuffer("Hello World"));
  });
}

TEST_F(ZlibServerFilterTest, multiple_accepted_encodings_qvalues) {
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(true,
                         std::string("http://locahost/foo.compressme"),
                         std::string("gzip; q=.7;, identity"),
                         std::string("gzip"),
                         std::string("Hello World"),
                         std::string("text/html"),
                         folly::IOBuf::copyBuffer("Hello World"));
  });
}

TEST_F(ZlibServerFilterTest, no_compressible_accepted_encodings) {
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(false,
                         std::string("http://locahost/foo.compressme"),
                         std::string("identity; q=.7;"),
                         std::string(""),
                         std::string("Hello World"),
                         std::string("text/html"),
                         folly::IOBuf::copyBuffer("Hello World"));
  });
}

TEST_F(ZlibServerFilterTest, missing_accepted_encodings) {
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(false,
                         std::string("http://locahost/foo.compressme"),
                         std::string(""),
                         std::string(""),
                         std::string("Hello World"),
                         std::string("text/html"),
                         folly::IOBuf::copyBuffer("Hello World"));
  });
}

// Content is of an-uncompressible content-type
TEST_F(ZlibServerFilterTest, uncompressible_contenttype) {
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(false,
                         std::string("http://locahost/foo.nocompress"),
                         std::string("gzip"),
                         std::string(""),
                         std::string("Hello World"),
                         std::string("image/jpeg"),
                         folly::IOBuf::copyBuffer("Hello World"));
  });
}

TEST_F(ZlibServerFilterTest, uncompressible_contenttype_param) {
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(false,
                         std::string("http://locahost/foo.nocompress"),
                         std::string("gzip"),
                         std::string(""),
                         std::string("Hello World"),
                         std::string("application/jpeg; param1"),
                         folly::IOBuf::copyBuffer("Hello World"));
  });
}

// Content is under the minimum compression size
TEST_F(ZlibServerFilterTest, too_small_to_compress) {
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(false,
                         std::string("http://locahost/foo.smallfry"),
                         std::string("gzip"),
                         std::string(""),
                         std::string("Hello World"),
                         std::string("text/html"),
                         folly::IOBuf::copyBuffer("Hello World"),
                         4,
                         1000);
  });
}

TEST_F(ZlibServerFilterTest, small_chunks_compress) {
  // Expect this to compress despite being small because can't tell the content
  // length when we're chunked
  std::vector<std::string> chunks = {"Hello", " World"};
  ASSERT_NO_FATAL_FAILURE({
    exercise_compression(true,
                         std::string("http://locahost/foo.compressme"),
                         std::string("gzip"),
                         std::string("gzip"),
                         std::string("Hello World"),
                         std::string("text/html"),
                         createResponseChain(chunks),
                         4,
                         1000);
  });
}
