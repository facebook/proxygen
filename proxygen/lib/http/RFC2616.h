/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Range.h>
#include <proxygen/lib/http/HTTPMethod.h>
#include <string>

namespace proxygen {

class HTTPHeaders;

namespace RFC2616 {

/**
 * This file contains functions for determining when certain tricky parts of RFC
 * 2616 arise.
 */

/**
 * The HTTP request as defined in RFC 2616 may or may not have a body. In some
 * cases they MUST NOT have a body. In other cases, the body has no semantic
 * meaning and so is not defined. Finally, for some methods, the body is well
 * defined. Please see Section 9 and 4.3 for details on this.
 */
enum class BodyAllowed {
  DEFINED,
  NOT_DEFINED,
  NOT_ALLOWED,
};
BodyAllowed isRequestBodyAllowed(boost::optional<HTTPMethod> method);

/**
 * Some status codes imply that there MUST NOT be a response body.  See section
 * 4.3: "All 1xx (informational), 204 (no content), and 304 (not modified)
 * responses MUST NOT include a message-body."
 * @param status The code to test (100 <= status <= 999)
 */
bool responseBodyMustBeEmpty(unsigned status);

/**
 * Returns true if the headers imply that a body will follow. Note that in some
 * situations a body may come even if this function returns false (e.g. a 1.0
 * response body's length can be given implicitly by closing the connection).
 */
bool bodyImplied(const HTTPHeaders& headers);

/**
 * Parse a string containing tokens and qvalues, such as the RFC strings for
 * Accept-Charset, Accept-Encoding and Accept-Language.  It won't work for
 * complex Accept: headers because it doesn't return parameters or
 * accept-extension.
 *
 * See RFC sections 14.2, 14.3, 14.4 for definitions of these header values
 *
 * TODO: optionally sort by qvalue descending
 *
 * Return true if the string was well formed according to the RFC.  Note it can
 * return false but still populate output with best-effort parsing.
 */
typedef std::pair<folly::StringPiece, double> TokenQPair;

bool parseQvalues(folly::StringPiece value, std::vector<TokenQPair> &output);

/**
 * Parse an RFC 2616 section 14.16 "bytes A-B/C" string and returns them as the
 * first and last bytes and instance length, respectively.
 *
 * Wildcards are handled specially as follows: if the range is actually "*",
 * the first byte is parsed as 0 and last byte as ULONG_MAX; if instance length
 * is actually "*", it is parsed as ULONG_MAX.
 *
 * Note that is ONLY suitable for use in parsing "Content-Range" response
 * headers. The "Range" request header has different but similar syntax.
 */
bool parseByteRangeSpec(
    folly::StringPiece value,
    unsigned long& firstByte,
    unsigned long& lastByte,
    unsigned long& instanceLength);

}}
