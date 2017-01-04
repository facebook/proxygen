/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/test/HTTPArchive.h>

#include <algorithm>
#include <folly/io/IOBuf.h>
#include <folly/json.h>
#include <fstream>
#include <glog/logging.h>
#include <ios>
#include <string>

using folly::IOBuf;
using std::ifstream;
using std::ios;
using std::string;
using std::unique_ptr;
using std::vector;

namespace proxygen {

std::unique_ptr<IOBuf> readFileToIOBuf(const std::string& filename) {
  // read the contents of the file
  ifstream file(filename);
  if (!file.is_open()) {
    LOG(ERROR) << "could not open file '" << filename << "'";
    return nullptr;
  }
  file.seekg(0, ios::end);
  int64_t size = file.tellg();
  if (size < 0) {
    LOG(ERROR) << "failed to fetch the position at the end of the file";
    return nullptr;
  }
  file.seekg(0, ios::beg);
  unique_ptr<IOBuf> buffer = IOBuf::create(size + 1);
  file.read((char *)buffer->writableData(), size);
  buffer->writableData()[size] = 0;
  buffer->append(size + 1);
  if (!file) {
    LOG(ERROR) << "error occurred, was able to read only "
               << file.gcount() << " bytes out of " << size;
    return nullptr;
  }
  return buffer;
}

unique_ptr<HTTPArchive> HTTPArchive::fromFile(const string& filename) {
  unique_ptr<HTTPArchive> har = folly::make_unique<HTTPArchive>();
  auto buffer = readFileToIOBuf(filename);
  if (!buffer) {
    return nullptr;
  }
  folly::dynamic jsonObj = folly::parseJson((const char *)buffer->data());
  auto entries = jsonObj["log"]["entries"];
  vector<HPACKHeader> msg;
  // go over all the transactions
  for (size_t i = 0; i < entries.size(); i++) {
    extractHeaders(entries[i]["request"], msg);
    if (!msg.empty()) {
      har->requests.push_back(msg);
    }
    extractHeaders(entries[i]["response"], msg);
    if (!msg.empty()) {
      har->responses.push_back(msg);
    }
  }

  return har;
}

void HTTPArchive::extractHeaders(folly::dynamic& obj,
                                 vector<HPACKHeader> &msg) {
  msg.clear();
  auto& headersObj = obj["headers"];
  for (size_t i = 0; i < headersObj.size(); i++) {
    string name = headersObj[i]["name"].asString();
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    msg.push_back(
      HPACKHeader(
        name,
        headersObj[i]["value"].asString())
    );
  }
}

void HTTPArchive::extractHeadersFromPublic(folly::dynamic& obj,
                                           vector<HPACKHeader> &msg) {
  msg.clear();
  auto& headersObj = obj["headers"];
  for (size_t i = 0; i < headersObj.size(); i++) {
    auto& headerObj = headersObj[i];
    for (auto& k: headerObj.keys()) {
      string name = k.asString();
      string value = headerObj[name].asString();
      std::transform(name.begin(), name.end(), name.begin(), ::tolower);
      msg.push_back(HPACKHeader(name, value));
    }
  }
}

uint32_t HTTPArchive::getSize(const vector<HPACKHeader> &headers) {
  uint32_t size = 0;

  for (const auto header : headers) {
    size += header.name.size() + header.value.size() + 2;
  }
  return size;
}

unique_ptr<HTTPArchive> HTTPArchive::fromPublicFile(const string& filename) {
  unique_ptr<HTTPArchive> har = folly::make_unique<HTTPArchive>();
  auto buffer = readFileToIOBuf(filename);
  if (!buffer) {
    return nullptr;
  }
  folly::dynamic jsonObj = folly::parseJson((const char *)buffer->data());
  auto entries = jsonObj["cases"];
  vector<HPACKHeader> msg;
  // go over all the transactions
  for (size_t i = 0; i < entries.size(); i++) {
    extractHeadersFromPublic(entries[i], msg);
    if (!msg.empty()) {
      har->requests.push_back(msg);
    }
  }

  return har;
}

}
