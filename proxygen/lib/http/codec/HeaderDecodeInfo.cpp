/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/HeaderDecodeInfo.h>

#include <folly/Conv.h>
#include <proxygen/lib/http/codec/CodecUtil.h>

using std::string;

namespace proxygen {

bool HeaderDecodeInfo::onHeader(const HPACKHeaderName& name,
                                const folly::fbstring& value) {
  // Refuse decoding other headers if an error is already found
  if (decodeError != HPACK::DecodeError::NONE || !parsingError.empty()) {
    VLOG(4) << "Ignoring header=" << name << " value=" << value
            << " due to parser error=" << parsingError;
    return true;
  }
  DVLOG(5) << "Processing header=" << name << " value=" << value;
  auto headerCode = name.getHeaderCode();
  folly::StringPiece nameSp(name.get());
  folly::StringPiece valueSp(value);
  auto& headers = msg->getHeaders();

  if (nameSp.startsWith(':')) {
    pseudoHeaderSeen_ = true;
    if (firstPseudoHeader_.empty()) {
      firstPseudoHeader_ = nameSp.str();
    }
    if (regularHeaderSeen_) {
      parsingError = folly::to<string>("illegal-pseudo-header name=", nameSp);
      return false;
    }
    if (isRequest_) {
      bool ok = false;
      switch (headerCode) {
        case HTTP_HEADER_COLON_METHOD:
          ok = verifier.setMethod(valueSp);
          if (verifier.hasValidationError()) {
            proxygenError = kErrorHeaderContentValidation;
          }
          break;
        case HTTP_HEADER_COLON_SCHEME:
          ok = verifier.setScheme(valueSp);
          if (verifier.hasValidationError()) {
            proxygenError = kErrorHeaderContentValidation;
          }
          break;
        case HTTP_HEADER_COLON_AUTHORITY:
          ok = verifier.setAuthority(valueSp, validate_, strictValidation_);
          if (verifier.hasValidationError()) {
            proxygenError = kErrorHeaderContentValidation;
          }
          break;
        case HTTP_HEADER_COLON_PATH:
          ok = verifier.setPath(valueSp, strictValidation_, allowEmptyPath_);
          if (verifier.hasValidationError()) {
            proxygenError = kErrorHeaderContentValidation;
          }
          break;
        case HTTP_HEADER_COLON_PROTOCOL:
          ok = verifier.setUpgradeProtocol(valueSp, strictValidation_);
          if (verifier.hasValidationError()) {
            proxygenError = kErrorHeaderContentValidation;
          }
          break;
        default:
          parsingError =
              folly::to<string>("invalid-request-pseudo-header name=", nameSp);
          return false;
      }
      if (!ok) {
        return false;
      }
    } else {
      if (headerCode == HTTP_HEADER_COLON_STATUS) {
        if (hasStatus_) {
          parsingError = folly::to<string>("duplicate-status existing=",
                                           msg->getStatusCode(),
                                           " new=",
                                           valueSp);
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
          parsingError = folly::to<string>("malformed-status code=", valueSp);
          return false;
        }
      } else {
        parsingError =
            folly::to<string>("invalid-response-pseudo-header name=", nameSp);
        return false;
      }
    }
  } else {
    regularHeaderSeen_ = true;
    switch (headerCode) {
      case HTTP_HEADER_CONNECTION:
        parsingError =
            folly::to<string>("connection-header-forbidden value=", valueSp);
        return false;
      case HTTP_HEADER_CONTENT_LENGTH: {
        const auto cl = headers.getSingleOrNullptr(HTTP_HEADER_CONTENT_LENGTH);
        if (cl) {
          bool ok = *cl == valueSp;
          if (!ok) {
            parsingError = folly::to<string>(
                "multiple-content-length existing=", *cl, " new=", valueSp);
          }
          return ok; // skips adding if already present and equal
        }
        break;
      }
      case HTTP_HEADER_HOST: {
        if (verifier.hasAuthority()) { // HTTP_HEADER_HOST already present
          bool ok = headers.getSingleOrEmpty(HTTP_HEADER_HOST) == valueSp;
          if (!ok) {
            parsingError =
                folly::to<string>("authority-host-mismatch existing=",
                                  headers.getSingleOrEmpty(HTTP_HEADER_HOST),
                                  " new=",
                                  valueSp);
          }
          return ok; // skips adding if already present and equal
        }
        break;
      }
      default:
        // no op
        break;
    }
    bool nameOk = !validate_ || headerCode != HTTP_HEADER_OTHER ||
                  CodecUtil::validateHeaderName(
                      nameSp,
                      strictValidation_ ? CodecUtil::HEADER_NAME_STRICT
                                        : CodecUtil::HEADER_NAME_STRICT_COMPAT);
    auto valueError =
        validate_
            ? CodecUtil::validateHeaderValueDetail(
                  valueSp,
                  strictValidation_ ? CodecUtil::CtlEscapeMode::STRICT
                                    : CodecUtil::CtlEscapeMode::STRICT_COMPAT)
            : CodecUtil::HeaderValueError::None;
    if (!nameOk || valueError != CodecUtil::HeaderValueError::None) {
      proxygenError =
          nameSp.empty() ? kErrorParseHeader : kErrorHeaderContentValidation;
      std::string reason =
          nameSp.empty() ? "empty-name" : (!nameOk ? "invalid-name-char" : "");
      if (valueError != CodecUtil::HeaderValueError::None) {
        if (!reason.empty()) {
          reason += ",";
        }
        reason += CodecUtil::describeHeaderValueError(valueError);
      }
      parsingError = folly::to<string>(isRequestTrailers_ ? "invalid-trailer "
                                                          : "invalid-header ",
                                       "name=",
                                       nameSp,
                                       " reason=",
                                       reason);
      headerErrorValue = valueSp;
      return false;
    }

    // Add the (name, value) pair to headers
    headerCode == HTTP_HEADER_OTHER ? headers.add(nameSp, valueSp)
                                    : headers.add(headerCode, valueSp);
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
    parsingError = folly::to<string>("pseudo-header-in-trailers name=",
                                     firstPseudoHeader_);
    return;
  }

  msg->setHTTPVersion(1, 1);
  msg->setIngressHeaderSize(decodedSize);
}

bool HeaderDecodeInfo::hasStatus() const {
  return hasStatus_;
}
} // namespace proxygen
