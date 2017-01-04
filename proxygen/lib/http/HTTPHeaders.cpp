/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#define PROXYGEN_HTTPHEADERS_IMPL
#include <proxygen/lib/http/HTTPHeaders.h>

#include <folly/portability/GFlags.h>

#include <glog/logging.h>

using std::bitset;
using std::string;
using std::vector;

namespace proxygen {

const string empty_string;
const std::string HTTPHeaders::COMBINE_SEPARATOR = ", ";

bitset<256>& HTTPHeaders::perHopHeaderCodes() {
  static bitset<256> perHopHeaderCodes{
    [] {
      bitset<256> bs;
      bs[HTTP_HEADER_CONNECTION] = true;
      bs[HTTP_HEADER_KEEP_ALIVE] = true;
      bs[HTTP_HEADER_PROXY_AUTHENTICATE] = true;
      bs[HTTP_HEADER_PROXY_AUTHORIZATION] = true;
      bs[HTTP_HEADER_PROXY_CONNECTION] = true;
      bs[HTTP_HEADER_TE] = true;
      bs[HTTP_HEADER_TRAILER] = true;
      bs[HTTP_HEADER_TRANSFER_ENCODING] = true;
      bs[HTTP_HEADER_UPGRADE] = true;
      return bs;
    }()
  };
  return perHopHeaderCodes;
}

HTTPHeaders::HTTPHeaders() :
  deletedCount_(0) {
  codes_.reserve(kInitialVectorReserve);
  headerNames_.reserve(kInitialVectorReserve);
  headerValues_.reserve(kInitialVectorReserve);
}

void HTTPHeaders::add(folly::StringPiece name, folly::StringPiece value) {
  CHECK(name.size());
  const HTTPHeaderCode code = HTTPCommonHeaders::hash(name.data(), name.size());
  codes_.push_back(code);
  headerNames_.push_back((code == HTTP_HEADER_OTHER)
      ? new std::string(name.data(), name.size())
      : HTTPCommonHeaders::getPointerToHeaderName(code));
  headerValues_.emplace_back(value.data(), value.size());
}

void HTTPHeaders::rawAdd(const std::string& name, const std::string& value) {
  add(name, value);
}

void HTTPHeaders::addFromCodec(const char* str, size_t len, string&& value) {
  const HTTPHeaderCode code = HTTPCommonHeaders::hash(str, len);
  codes_.push_back(code);
  headerNames_.push_back((code == HTTP_HEADER_OTHER)
      ? new string(str, len)
      : HTTPCommonHeaders::getPointerToHeaderName(code));
  headerValues_.emplace_back(std::move(value));
}

bool HTTPHeaders::exists(folly::StringPiece name) const {
  const HTTPHeaderCode code = HTTPCommonHeaders::hash(name.data(),
                                                      name.size());
  if (code != HTTP_HEADER_OTHER) {
    return exists(code);
  } else {
    ITERATE_OVER_STRINGS(name, { return true; });
    return false;
  }
}

bool HTTPHeaders::exists(HTTPHeaderCode code) const {
  return memchr((void*)codes_.data(), code, codes_.size()) != nullptr;
}

size_t HTTPHeaders::getNumberOfValues(HTTPHeaderCode code) const {
  size_t count = 0;
  ITERATE_OVER_CODES(code, {
      (void)pos;
      ++count;
  });
  return count;
}

size_t HTTPHeaders::getNumberOfValues(folly::StringPiece name) const {
  size_t count = 0;
  forEachValueOfHeader(name, [&] (folly::StringPiece value) -> bool {
    ++count;
    return false;
  });
  return count;
}

bool HTTPHeaders::remove(folly::StringPiece name) {
  const HTTPHeaderCode code = HTTPCommonHeaders::hash(name.data(),
                                                      name.size());
  if (code != HTTP_HEADER_OTHER) {
    return remove(code);
  } else {
    bool removed = false;
    ITERATE_OVER_STRINGS(name, {
      delete headerNames_[pos];
      codes_[pos] = HTTP_HEADER_NONE;
      removed = true;
      ++deletedCount_;
    });
    return removed;
  }
}

bool HTTPHeaders::remove(HTTPHeaderCode code) {
  bool removed = false;
  ITERATE_OVER_CODES(code, {
    codes_[pos] = HTTP_HEADER_NONE;
    removed = true;
    ++deletedCount_;
  });
  return removed;
}

void HTTPHeaders::disposeOfHeaderNames() {
  for (size_t i = 0; i < codes_.size(); ++i) {
    if (codes_[i] == HTTP_HEADER_OTHER) {
      delete headerNames_[i];
    }
  }
}

HTTPHeaders::~HTTPHeaders () {
  disposeOfHeaderNames();
}

HTTPHeaders::HTTPHeaders(const HTTPHeaders& hdrs) :
  codes_(hdrs.codes_),
  headerNames_(hdrs.headerNames_),
  headerValues_(hdrs.headerValues_),
  deletedCount_(hdrs.deletedCount_) {
  for (size_t i = 0; i < codes_.size(); ++i) {
    if (codes_[i] == HTTP_HEADER_OTHER) {
      headerNames_[i] = new string(*hdrs.headerNames_[i]);
    }
  }
}

HTTPHeaders::HTTPHeaders(HTTPHeaders&& hdrs) noexcept :
    codes_(std::move(hdrs.codes_)),
    headerNames_(std::move(hdrs.headerNames_)),
    headerValues_(std::move(hdrs.headerValues_)),
    deletedCount_(hdrs.deletedCount_) {
  hdrs.removeAll();
}

HTTPHeaders& HTTPHeaders::operator= (const HTTPHeaders& hdrs) {
  if (this != &hdrs) {
    disposeOfHeaderNames();
    codes_ = hdrs.codes_;
    headerNames_ = hdrs.headerNames_;
    headerValues_ = hdrs.headerValues_;
    deletedCount_ = hdrs.deletedCount_;
    for (size_t i = 0; i < codes_.size(); ++i) {
      if (codes_[i] == HTTP_HEADER_OTHER) {
        headerNames_[i] = new string(*hdrs.headerNames_[i]);
      }
    }
  }
  return *this;
}

HTTPHeaders& HTTPHeaders::operator= (HTTPHeaders&& hdrs) {
  if (this != &hdrs) {
    codes_ = std::move(hdrs.codes_);
    headerNames_ = std::move(hdrs.headerNames_);
    headerValues_ = std::move(hdrs.headerValues_);
    deletedCount_ = hdrs.deletedCount_;

    hdrs.removeAll();
  }

  return *this;
}

void HTTPHeaders::removeAll() {
  disposeOfHeaderNames();

  codes_.clear();
  headerNames_.clear();
  headerValues_.clear();
  deletedCount_ = 0;
}

size_t HTTPHeaders::size() const {
  return codes_.size() - deletedCount_;
}

bool
HTTPHeaders::transferHeaderIfPresent(folly::StringPiece name,
                                     HTTPHeaders& strippedHeaders) {
  bool transferred = false;
  const HTTPHeaderCode code = HTTPCommonHeaders::hash(name.data(),
                                                      name.size());
  if (code == HTTP_HEADER_OTHER) {
    ITERATE_OVER_STRINGS(name, {
      strippedHeaders.codes_.push_back(HTTP_HEADER_OTHER);
      // in the next line, ownership of pointer goes to strippedHeaders
      strippedHeaders.headerNames_.push_back(headerNames_[pos]);
      strippedHeaders.headerValues_.push_back(headerValues_[pos]);
      codes_[pos] = HTTP_HEADER_NONE;
      transferred = true;
      ++deletedCount_;
    });
  } else { // code != HTTP_HEADER_OTHER
    ITERATE_OVER_CODES(code, {
      strippedHeaders.codes_.push_back(code);
      strippedHeaders.headerNames_.push_back(headerNames_[pos]);
      strippedHeaders.headerValues_.push_back(headerValues_[pos]);
      codes_[pos] = HTTP_HEADER_NONE;
      transferred = true;
      ++deletedCount_;
    });
  }
  return transferred;
}

void
HTTPHeaders::stripPerHopHeaders(HTTPHeaders& strippedHeaders) {
  int len;
  forEachValueOfHeader(HTTP_HEADER_CONNECTION, [&]
                       (const string& stdStr) -> bool {
    // Remove all headers specified in Connection header
    // look for multiple values separated by commas
    char const* str = stdStr.c_str();

    // skip leading whitespace
    while (isLWS(*str)) str++;

    while (*str != 0) {
      char const* pos = strchr(str, ',');
      if (pos == nullptr) {
        // last (or only) token, done

        // count chars in the token
        len = 0;
        while (str[len] != 0 && !isLWS(str[len])) len++;
        if (len > 0) {
          string hdr(str, len);
          if (transferHeaderIfPresent(hdr, strippedHeaders)) {
            VLOG(3) << "Stripped connection-named hop-by-hop header " << hdr;
          }
        }
        break;
      }
      len = pos - str;
      // strip trailing whitespace
      while (len > 0 && isLWS(str[len - 1])) len--;
      if (len > 0) {
        // non-empty token
        string hdr(str, len);
        if (transferHeaderIfPresent(hdr, strippedHeaders)) {
          VLOG(3) << "Stripped connection-named hop-by-hop header " << hdr;
        }
      } // else empty token, no-op
      str = pos + 1;

      // skip whitespace
      while (isLWS(*str)) str++;
    }
    return false; // continue processing "connection" headers
  });

  // Strip hop-by-hop headers
  auto& perHopHeaders = perHopHeaderCodes();
  for (size_t i = 0; i < codes_.size(); ++i) {
    if (perHopHeaders[codes_[i]]) {
      strippedHeaders.codes_.push_back(codes_[i]);
      strippedHeaders.headerNames_.push_back(headerNames_[i]);
      strippedHeaders.headerValues_.push_back(headerValues_[i]);
      codes_[i] = HTTP_HEADER_NONE;
      ++deletedCount_;
      VLOG(5) << "Stripped hop-by-hop header " << *headerNames_[i];
    }
  }
}

void HTTPHeaders::copyTo(HTTPHeaders& hdrs) const {
  for (size_t i = 0; i < codes_.size(); ++i) {
    if (codes_[i] != HTTP_HEADER_NONE) {
      hdrs.codes_.push_back(codes_[i]);
      hdrs.headerNames_.push_back((codes_[i] == HTTP_HEADER_OTHER) ?
          new string(*headerNames_[i]) : headerNames_[i]);
      hdrs.headerValues_.push_back(headerValues_[i]);
    }
  }
}

}
