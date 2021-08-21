/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/HeaderDecodeInfo.h>

#include <folly/Conv.h>
#include <folly/String.h>
#include <proxygen/lib/http/codec/CodecUtil.h>

using std::string;

namespace proxygen {

bool HeaderDecodeInfo::onHeader(const HPACKHeaderName& name,
                                const folly::fbstring& value) {
  // Refuse decoding other headers if an error is already found
  if (decodeError != HPACK::DecodeError::NONE || parsingError != "") {
    VLOG(4) << "Ignoring header=" << name << " value=" << value
            << " due to parser error=" << parsingError;
    return true;
  }
  VLOG(5) << "Processing header=" << name << " value=" << value;
  auto headerCode = name.getHeaderCode();
  folly::StringPiece nameSp(name.get());
  folly::StringPiece valueSp(value);

  if (nameSp.startsWith(':')) {
    pseudoHeaderSeen_ = true;
    if (regularHeaderSeen_) {
      parsingError = folly::to<string>("Illegal pseudo header name=", nameSp);
      return false;
    }
    if (isRequest_) {
      bool ok = false;
      switch (headerCode) {
        case HTTP_HEADER_COLON_METHOD:
          ok = verifier.setMethod(valueSp);
          break;
        case HTTP_HEADER_COLON_SCHEME:
          ok = verifier.setScheme(valueSp);
          break;
        case HTTP_HEADER_COLON_AUTHORITY:
          ok = verifier.setAuthority(valueSp, validate_);
          break;
        case HTTP_HEADER_COLON_PATH:
          ok = verifier.setPath(valueSp);
          break;
        case HTTP_HEADER_COLON_PROTOCOL:
          ok = verifier.setUpgradeProtocol(valueSp);
          break;
        default:
          parsingError = folly::to<string>("Invalid req header name=", nameSp);
          return false;
      }
      if (!ok) {
        return false;
      }
    } else {
      if (headerCode == HTTP_HEADER_COLON_STATUS) {
        if (hasStatus_) {
          parsingError = string("Duplicate status");
          return false;
        }
        hasStatus_ = true;
        int32_t code = -1;
        folly::tryTo<int32_t>(valueSp).then(
            [&code](int32_t num) { code = num; });
        if (code >= 100 && code <= 999) {
          msg->setStatusCode(code);
          msg->setStatusMessage(HTTPMessage::getDefaultReason(code));
        } else {
          parsingError = folly::to<string>("Malformed status code=", valueSp);
          return false;
        }
      } else {
        parsingError = folly::to<string>("Invalid resp header name=", nameSp);
        return false;
      }
    }
  } else {
    regularHeaderSeen_ = true;
    switch (headerCode) {
      case HTTP_HEADER_CONNECTION:
        parsingError = string("HTTP/2 Message with Connection header");
        return false;
      case HTTP_HEADER_CONTENT_LENGTH: {
        uint32_t cl = 0;
        folly::tryTo<uint32_t>(valueSp).then([&cl](uint32_t num) { cl = num; });
        if (contentLength_ && *contentLength_ != cl) {
          parsingError = string("Multiple content-length headers");
          return false;
        }
        contentLength_ = cl;
        break;
      }
      default:
        // no op
        break;
    }
    bool nameOk = !validate_ || headerCode != HTTP_HEADER_OTHER ||
                  CodecUtil::validateHeaderName(
                      nameSp, CodecUtil::HEADER_NAME_STRICT_COMPAT);
    bool valueOk = !validate_ || CodecUtil::validateHeaderValue(
                                     valueSp, CodecUtil::STRICT_COMPAT);
    if (!nameOk || !valueOk) {
      parsingError = folly::to<string>(
          "Bad header value: name=", nameSp, " value=", valueSp);
      return false;
    }
    // Add the (name, value) pair to headers
    if (headerCode == HTTP_HEADER_OTHER) {
      msg->getHeaders().add(nameSp, valueSp);
    } else {
      msg->getHeaders().add(headerCode, valueSp);
    }
  }
  return true;
}

void HeaderDecodeInfo::onHeadersComplete(HTTPHeaderSize decodedSize) {
  HTTPHeaders& headers = msg->getHeaders();

  if (isRequest_ && !isRequestTrailers_) {
    auto combinedCookie = headers.combine(HTTP_HEADER_COOKIE, "; ");
    if (!combinedCookie.empty()) {
      headers.set(HTTP_HEADER_COOKIE, combinedCookie);
    }
    if (!verifier.validate()) {
      parsingError = verifier.error;
      return;
    }
  }

  bool isResponseTrailers = (!isRequest_ && !hasStatus_);
  if ((isRequestTrailers_ || isResponseTrailers) && pseudoHeaderSeen_) {
    parsingError = "Pseudo headers forbidden in trailers.";
    return;
  }

  msg->setHTTPVersion(1, 1);
  msg->setIngressHeaderSize(decodedSize);
}

bool HeaderDecodeInfo::hasStatus() const {
  return hasStatus_;
}
} // namespace proxygen
