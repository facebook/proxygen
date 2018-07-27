/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "StructuredHeadersUtilities.h"
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include "StructuredHeadersConstants.h"

namespace proxygen {
namespace StructuredHeaders {

bool isLcAlpha(char c) {
  return c >= 0x61 && c <= 0x7A;
}

bool isValidIdentifierChar(char c) {
  return isLcAlpha(c) || std::isdigit(c) || c == '_' || c == '-' || c == '*' ||
    c == '/';
}

bool isValidEncodedBinaryContentChar(
   char c) {
  return std::isalpha(c) || std::isdigit(c) || c == '+' || c == '/' || c == '=';
}

bool isValidStringChar(char c) {
  /*
  * The difference between the character restriction here and that mentioned
  * in section 3.7 of version 6 of the Structured Headers draft is that this
  * function accepts \ and DQUOTE characters. These characters are allowed
  * as long as they are present as a part of an escape sequence, which is
  * checked for in the parseString() function in the StructuredHeadersBuffer.
  */
  return c >= 0x20 && c <= 0x7E;
}

bool isValidIdentifier(const std::string& s) {
  if (s.size() == 0 || !isLcAlpha(s[0])) {
    return false;
  }

  for (char c : s) {
    if (!isValidIdentifierChar(c)) {
      return false;
    }
  }

  return true;
}

bool isValidString(const std::string& s) {
  for (char c : s) {
    if (!isValidStringChar(c)) {
      return false;
    }
  }
  return true;
}

bool isValidEncodedBinaryContent(
  const std::string& s) {

  if (s.size() % 4 != 0) {
    return false;
  }

  bool equalSeen = false;
  for (auto it = s.begin(); it != s.end(); it++) {
    if (*it == '=') {
      equalSeen = true;
    } else if (equalSeen || !isValidEncodedBinaryContentChar(*it)) {
      return false;
    }
  }

  return true;
}

bool itemTypeMatchesContent(
   const StructuredHeaderItem& input) {
  switch (input.tag) {
    case StructuredHeaderItem::Type::BINARYCONTENT:
    case StructuredHeaderItem::Type::IDENTIFIER:
    case StructuredHeaderItem::Type::STRING:
      return input.value.type() == typeid(std::string);
    case StructuredHeaderItem::Type::INT64:
      return input.value.type() == typeid(int64_t);
    case StructuredHeaderItem::Type::DOUBLE:
      return input.value.type() == typeid(double);
    case StructuredHeaderItem::Type::NONE:
      return true;
  }

  return false;
}

std::string decodeBase64(
    const std::string& encoded) {

  if (encoded.size() == 0) {
    // special case, to prevent an integer overflow down below.
    return "";
  }

  using namespace boost::archive::iterators;
  using b64it =
    transform_width<binary_from_base64<std::string::const_iterator>, 8, 6>;

  std::string decoded = std::string(b64it(std::begin(encoded)),
                                    b64it(std::end(encoded)));

  uint32_t numPadding = std::count(encoded.begin(), encoded.end(), '=');
  decoded.erase(decoded.end() - numPadding, decoded.end());

  return decoded;
}

std::string encodeBase64(const std::string& input) {
  using namespace boost::archive::iterators;
  using b64it = base64_from_binary<transform_width<const char*, 6, 8>>;

  auto data = input.data();
  std::string encoded(b64it(data), b64it(data + (input.length())));
  encoded.append((3 - (input.length() % 3)) % 3, '=');

  return encoded;
}

}
}
