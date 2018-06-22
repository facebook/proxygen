/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/init/Init.h>
#include <folly/io/Cursor.h>
#include <folly/portability/GFlags.h>
#include <proxygen/lib/http/codec/compress/QPACKCodec.h>
#include <proxygen/lib/http/codec/compress/experimental/simulator/CompressionUtils.h>
#include <proxygen/lib/http/codec/compress/experimental/simulator/SimStreamingCallback.h>
#include <proxygen/lib/http/codec/compress/test/HTTPArchive.h>

using namespace proxygen;
using namespace proxygen::compress;
using namespace folly;
using namespace folly::io;

DEFINE_string(output, "compress.out", "Output file for encoding");
DEFINE_string(input, "compress.in", "Input file for decoding");
DEFINE_string(har, "", "HAR file to compress or compare");
DEFINE_string(mode, "encode", "<encode|decode>");
DEFINE_bool(ack, true, "Encoder assumes immediate ack of all frames");
DEFINE_int32(table_size, 4096, "Dynamic table size");
DEFINE_int32(max_blocking, 100, "Max blocking streams");

void writeFrame(folly::io::QueueAppender& appender,
                uint64_t streamId,
                std::unique_ptr<folly::IOBuf> buf) {
  appender.writeBE<uint64_t>(streamId);
  appender.writeBE<uint32_t>(buf->computeChainDataLength());
  appender.insert(std::move(buf));
}

void encodeHar(const proxygen::HTTPArchive& har) {
  uint64_t streamId = 4;
  QPACKCodec encoder;
  encoder.setMaxVulnerable(FLAGS_max_blocking);
  encoder.setEncoderHeaderTableSize(FLAGS_table_size);
  folly::File outputF(FLAGS_output, O_CREAT | O_RDWR | O_TRUNC);
  IOBufQueue outbuf;
  QueueAppender appender(&outbuf, 1000);
  QPACKDecoder decoder;
  uint64_t bytesIn = 0;
  uint64_t bytesOut = 0;
  for (auto& req : har.requests) {
    std::vector<std::string> cookies;
    auto encoderInput = prepareMessageForCompression(req, cookies);
    auto result = encoder.encode(encoderInput, streamId);
    // always write stream before control to test decoder blocking
    if (result.stream) {
      writeFrame(appender, streamId, std::move(result.stream));
      if (FLAGS_ack) {
        encoder.decodeDecoderStream(decoder.encodeHeaderAck(streamId));
      }
    }
    if (result.control) {
      writeFrame(appender, 0, std::move(result.control));
      // Shouldn't need any TSS
    }
    bytesIn += encoder.getEncodedSize().uncompressed;
    auto out = outbuf.move();
    auto iov = out->getIov();
    bytesOut += writevFull(outputF.fd(), iov.data(), iov.size());
    streamId += 4;
  }
  LOG(INFO) << "Encoded " << (streamId / 4 - 1) << " streams.  Bytes in="
            << bytesIn << " Bytes out=" << bytesOut << " Ratio="
            << uint32_t(100 * (1 - (bytesOut / double(bytesIn))));
}

int decodeAndVerify(const proxygen::HTTPArchive& har) {
  QPACKCodec decoder;
  decoder.setMaxBlocking(FLAGS_max_blocking);
  decoder.setDecoderHeaderTableMaxSize(FLAGS_table_size);
  folly::File inputF(FLAGS_input, O_RDONLY);
  folly::IOBufQueue inbuf;
  enum { HEADER, DATA } state = HEADER;
  uint64_t streamId = 0;
  uint32_t length = 0;
  ssize_t rc = -1;
  std::map<uint64_t, SimStreamingCallback> streams;
  while (rc != 0) {
    auto pre = inbuf.preallocate(4096, 4096);
    rc = readNoInt(inputF.fd(), pre.first, pre.second);
    if (rc < 0) {
      LOG(ERROR) << "Read failed on " << FLAGS_input;
      return 1;
    }
    inbuf.postallocate(rc);
  };
  while (!inbuf.empty()) {
    if (state == HEADER) {
      Cursor c(inbuf.front());
      streamId = c.readBE<uint64_t>();
      length = c.readBE<uint32_t>();
      inbuf.trimStart(sizeof(uint64_t) + sizeof(uint32_t));
      state = DATA;
    }
    if (state == DATA) {
      Cursor c(inbuf.front());
      if (streamId == 0) {
        CHECK_EQ(decoder.decodeControl(c, length), HPACK::DecodeError::NONE);
      } else {
        std::unique_ptr<folly::IOBuf> data;
        c.clone(data, length);
        auto res = streams.emplace(std::piecewise_construct,
                                   std::forward_as_tuple(streamId),
                                   std::forward_as_tuple(streamId, nullptr));
        decoder.decodeStreaming(std::move(data), length, &res.first->second);
      }
      inbuf.trimStart(length);
      state = HEADER;
    }
  }

  size_t i = 0;
  for (const auto& req : streams) {
    if (req.second.error != HPACK::DecodeError::NONE) {
      LOG(ERROR) << "request=" << req.first
                 << " failed to decode error=" << req.second.error;
      return 1;
    }
    if (!(req.second.msg == har.requests[i])) {
      LOG(ERROR) << "requests are not equal, got=" << req.second.msg
                 << " expected=" << har.requests[i];
    }
    i++;
  }
  LOG(INFO) << "Verified " << i << " streams.";
  return 0;
}

int main(int argc, char** argv) {
  folly::init(&argc, &argv, true);
  std::unique_ptr<HTTPArchive> har = HTTPArchive::fromFile(FLAGS_har);
  if (!har) {
    LOG(ERROR) << "Failed to read har file='" << FLAGS_har << "'";
    return 1;
  }
  if (FLAGS_mode == "encode") {
    encodeHar(*har);
  } else if (FLAGS_mode == "decode") {
    return decodeAndVerify(*har);
  } else {
    LOG(ERROR) << "Usage" << std::endl;
    return 1;
  }
  return 0;
}
