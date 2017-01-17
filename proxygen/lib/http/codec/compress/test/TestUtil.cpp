/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/test/TestUtil.h>

#include <fstream>
#include <glog/logging.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/codec/compress/Logging.h>

using folly::IOBuf;
using std::ofstream;
using std::string;
using std::unique_ptr;
using std::vector;

namespace proxygen { namespace hpack {

void dumpToFile(const string& filename, const IOBuf* buf) {
  ofstream outfile(filename, ofstream::binary);
  if (buf) {
    const IOBuf* p = buf;
    do {
      outfile.write((const char *)p->data(), p->length());
      p = p->next();
    } while (p->next() != buf);
  }
  outfile.close();
}

unique_ptr<IOBuf> encodeDecode(
    vector<HPACKHeader> headers,
    HPACKEncoder& encoder,
    HPACKDecoder& decoder) {
  unique_ptr<IOBuf> encoded = encoder.encode(headers);
  auto decodedHeaders = decoder.decode(encoded.get());
  CHECK(!decoder.hasError());

  EXPECT_EQ(headers.size(), decodedHeaders->size());
  sort(decodedHeaders->begin(), decodedHeaders->end());
  sort(headers.begin(), headers.end());
  if (headers.size() != decodedHeaders->size()) {
    std::cerr << printDelta(*decodedHeaders, headers);
    CHECK(false) << "Mismatched headers size";
  }
  EXPECT_EQ(headers, *decodedHeaders);
  if (headers != *decodedHeaders) {
    std::cerr << printDelta(headers, *decodedHeaders);
    CHECK(false) << "Mismatched headers";
  }
  // header tables should look the same
  CHECK(encoder.getTable() == decoder.getTable());
  EXPECT_EQ(encoder.getTable(), decoder.getTable());

  return encoded;
}

}}
