/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/RFC2616.h>

#include <stdlib.h>

#include <folly/String.h>
#include <folly/ThreadLocal.h>
#include <proxygen/lib/http/HTTPHeaders.h>

namespace {

/* Wapper around strtoul(3) */
bool strtoulWrapper(const char *&curs, const char *end, unsigned long& val) {
  char* endptr = nullptr;

  unsigned long v = strtoul(curs, &endptr, 10);
  if (endptr == curs) {
    return false;
  }

  if (endptr > end) {
    return false;
  }

  curs = endptr;
  val = v;

  return true;
}

}

namespace proxygen { namespace RFC2616 {

BodyAllowed isRequestBodyAllowed(boost::optional<HTTPMethod> method) {
  if (method == HTTPMethod::TRACE) {
    return BodyAllowed::NOT_ALLOWED;
  }
  if (method == HTTPMethod::OPTIONS || method == HTTPMethod::POST ||
      method == HTTPMethod::PUT || method == HTTPMethod::CONNECT) {
    return BodyAllowed::DEFINED;
  }
  return BodyAllowed::NOT_DEFINED;
}

bool responseBodyMustBeEmpty(unsigned status) {
  return (status == 304 || status == 204 ||
          (100 <= status && status < 200));
}

bool bodyImplied(const HTTPHeaders& headers) {
  return headers.exists(HTTP_HEADER_TRANSFER_ENCODING) ||
    headers.exists(HTTP_HEADER_CONTENT_LENGTH);
}

bool parseQvalues(folly::StringPiece value, std::vector<TokenQPair> &output) {
  bool result = true;
  static folly::ThreadLocal<std::vector<folly::StringPiece>> tokens;
  tokens->clear();
  folly::split(",", value, *tokens, true /*ignore empty*/);
  for (auto& token: *tokens) {
    auto pos = token.find(';');
    double qvalue = 1.0;
    if (pos != std::string::npos) {
      auto qpos = token.find("q=", pos);
      if (qpos != std::string::npos) {
        folly::StringPiece qvalueStr(token.data() + qpos + 2,
                                     token.size() - (qpos + 2));
        try {
          qvalue = folly::to<double>(&qvalueStr);
        } catch (const std::range_error&) {
          // q=<some garbage>
          result = false;
        }
        // we could validate that the remainder of qvalueStr was all whitespace,
        // for now we just discard it
      } else {
        // ; but no q=
        result = false;
      }
      token.reset(token.start(), pos);
    }
    // strip leading whitespace
    while (token.size() > 0 && isspace(token[0])) {
      token.reset(token.start() + 1, token.size() - 1);
    }
    if (token.size() == 0) {
      // empty token
      result = false;
    } else {
      output.emplace_back(token, qvalue);
    }
  }
  return result && output.size() > 0;
}

bool parseByteRangeSpec(
    folly::StringPiece value,
    unsigned long& outFirstByte,
    unsigned long& outLastByte,
    unsigned long& outInstanceLength) {
  // We should start with "bytes "
  if (!value.startsWith("bytes ")) {
    return false;
  }

  const char* curs = value.begin() + 6 /* strlen("bytes ") */;
  const char* end = value.end();

  unsigned long firstByte = ULONG_MAX;
  unsigned long lastByte = ULONG_MAX;
  unsigned long instanceLength = ULONG_MAX;

  if (!strtoulWrapper(curs, end, firstByte)) {
    if (*curs != '*') {
      return false;
    }

    firstByte = 0;
    lastByte = ULONG_MAX;
    ++curs;
  } else {
    if (*curs != '-') {
      return false;
    }

    ++curs;

    if (!strtoulWrapper(curs, end, lastByte)) {
      return false;
    }
  }

  if (*curs != '/') {
    return false;
  }

  ++curs;
  if (*curs != '*') {
    if (!strtoulWrapper(curs, end, instanceLength)) {
      return false;
    }
  } else {
    ++curs;
  }

  if (curs < end && *curs != '\0') {
    return false;
  }

  if (lastByte < firstByte) {
    return false;
  }

  if ((lastByte - firstByte + 1) > instanceLength) {
    return false;
  }

  outFirstByte = firstByte;
  outLastByte = lastByte;
  outInstanceLength = instanceLength;
  return true;
}

}}
