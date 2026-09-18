/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/lib/http/codec/CodecUtil.h>

#include <folly/portability/GTest.h>

using std::string;

namespace proxygen::test {

folly::ByteRange input(const char *str) {
  return {reinterpret_cast<const uint8_t *>(str), strlen(str)};
}

TEST(CodecUtil, validateURL) {
  EXPECT_TRUE(CodecUtil::validateURL("/foo", URLValidateMode::STRICT));
  EXPECT_TRUE(
      CodecUtil::validateURL("/foo\xff", URLValidateMode::STRICT_COMPAT));
  EXPECT_FALSE(CodecUtil::validateURL("/foo\xff", URLValidateMode::STRICT));
}

TEST(CodecUtil, validateMethod) {
  EXPECT_TRUE(CodecUtil::validateMethod(input("GET")));
  EXPECT_TRUE(CodecUtil::validateMethod(input("CONNECT-UDP")));
  // TODO:
  // EXPECT_FALSE(CodecUtil::validateMethod(input("CONNECT-")));
  EXPECT_FALSE(CodecUtil::validateMethod(input("-UDP")));
  EXPECT_FALSE(CodecUtil::validateMethod(input("-")));
  EXPECT_TRUE(CodecUtil::validateMethod(input("lowercase")));
}

TEST(CodecUtil, validateScheme) {
  EXPECT_TRUE(CodecUtil::validateScheme(input("http")));
  EXPECT_TRUE(CodecUtil::validateScheme(input("foo")));
  EXPECT_FALSE(CodecUtil::validateScheme(input("h1th3r3")));
}

TEST(CodecUtil, validateHeaderName) {
  EXPECT_TRUE(CodecUtil::validateHeaderName(input("foo"),
                                            CodecUtil::HEADER_NAME_STRICT));
  EXPECT_TRUE(CodecUtil::validateHeaderName(input("foo_bar"),
                                            CodecUtil::HEADER_NAME_STRICT));
  EXPECT_TRUE(CodecUtil::validateHeaderName(input("foo-bar"),
                                            CodecUtil::HEADER_NAME_STRICT));
  EXPECT_FALSE(
      CodecUtil::validateHeaderName(input(""), CodecUtil::HEADER_NAME_STRICT));
  EXPECT_FALSE(CodecUtil::validateHeaderName(input(":foo"),
                                             CodecUtil::HEADER_NAME_STRICT));
  EXPECT_FALSE(CodecUtil::validateHeaderName(input("foo:bar"),
                                             CodecUtil::HEADER_NAME_STRICT));
  EXPECT_FALSE(CodecUtil::validateHeaderName(input("foo\xf0"),
                                             CodecUtil::HEADER_NAME_STRICT));
  EXPECT_FALSE(CodecUtil::validateHeaderName(input("foo\r"),
                                             CodecUtil::HEADER_NAME_STRICT));
  EXPECT_FALSE(CodecUtil::validateHeaderName(input("foo\n"),
                                             CodecUtil::HEADER_NAME_STRICT));
  EXPECT_FALSE(CodecUtil::validateHeaderName(input("foo\r\nfoo"),
                                             CodecUtil::HEADER_NAME_STRICT));

  std::array<char, 19> httpSeparators{'(',
                                      ')',
                                      '<',
                                      '>',
                                      '@',
                                      ',',
                                      ';',
                                      ':',
                                      '\\',
                                      '\"',
                                      '/',
                                      '[',
                                      ']',
                                      '?',
                                      '=',
                                      '{',
                                      '}',
                                      ' ',
                                      '\t'};
  for (auto sep : httpSeparators) {
    auto testHeader = folly::to<std::string>("foo", sep, "bar");
    EXPECT_FALSE(CodecUtil::validateHeaderName(input(testHeader.c_str()),
                                               CodecUtil::HEADER_NAME_STRICT));
    if (sep == ' ' || sep == '/' || sep == '}' || sep == '"') {
      EXPECT_TRUE(CodecUtil::validateHeaderName(
          input(testHeader.c_str()), CodecUtil::HEADER_NAME_STRICT_COMPAT));
    }
  }
}

TEST(CodecUtil, validateHeaderValue) {
  EXPECT_TRUE(CodecUtil::validateHeaderValue(input("abc"), CodecUtil::STRICT));
  string allTheChars;
  allTheChars.reserve(127 - 32);
  for (uint8_t i = 32; i < 127; i++) {
    allTheChars += folly::to<char>(i);
  }
  EXPECT_TRUE(CodecUtil::validateHeaderValue(input(allTheChars.c_str()),
                                             CodecUtil::STRICT));
  // test without leading whitespace
  EXPECT_TRUE(CodecUtil::validateHeaderValue(input(allTheChars.c_str() + 1),
                                             CodecUtil::STRICT));

  // valid lws
  EXPECT_TRUE(
      CodecUtil::validateHeaderValue(input("abc\r\n\tdef"), CodecUtil::STRICT));
  EXPECT_TRUE(CodecUtil::validateHeaderValue(input("abc\r\n \t \t def"),
                                             CodecUtil::STRICT));
  // Invalid lws
  EXPECT_FALSE(CodecUtil::validateHeaderValue(input("abc\r \t \t def"),
                                              CodecUtil::STRICT));
  EXPECT_FALSE(
      CodecUtil::validateHeaderValue(input("abc\r\ndef"), CodecUtil::STRICT));
  // terminating open quote
  EXPECT_TRUE(
      CodecUtil::validateHeaderValue(input("abc\""), CodecUtil::STRICT));
  // open quote
  EXPECT_TRUE(
      CodecUtil::validateHeaderValue(input("abc\"def"), CodecUtil::STRICT));
  // quoted def
  EXPECT_TRUE(
      CodecUtil::validateHeaderValue(input("abc\"def\""), CodecUtil::STRICT));
  // quoted, escaped CRLF
  EXPECT_TRUE(CodecUtil::validateHeaderValue(input("abc\"\\\r\\\n\""),
                                             CodecUtil::COMPLIANT));
  EXPECT_FALSE(CodecUtil::validateHeaderValue(input("abc\"\\\r\\\n\""),
                                              CodecUtil::STRICT));
  EXPECT_TRUE(CodecUtil::validateHeaderValue(input("abc\xff"),
                                             CodecUtil::STRICT_COMPAT));
  EXPECT_FALSE(
      CodecUtil::validateHeaderValue(input("abc\xff"), CodecUtil::STRICT));
  EXPECT_FALSE(
      CodecUtil::validateHeaderValue(input("abc\x7f"), CodecUtil::STRICT));
  // hard-tab OK
  EXPECT_TRUE(
      CodecUtil::validateHeaderValue(input("abc\t"), CodecUtil::STRICT));

  // End on escape
  EXPECT_FALSE(
      CodecUtil::validateHeaderValue(input("\"\\"), CodecUtil::COMPLIANT));
  // End on partial LWS
  EXPECT_FALSE(
      CodecUtil::validateHeaderValue(input("foo\r"), CodecUtil::COMPLIANT));
  EXPECT_FALSE(
      CodecUtil::validateHeaderValue(input("foo\r\n"), CodecUtil::COMPLIANT));

  // leading white space stripped (copied EXPECT_TRUE cases from above and
  // added ws to beginning)
  EXPECT_TRUE(
      CodecUtil::validateHeaderValue(input("\tabc\t"), CodecUtil::STRICT));
  EXPECT_TRUE(CodecUtil::validateHeaderValue(input(" abc\r\n\tdef"),
                                             CodecUtil::STRICT));
  EXPECT_TRUE(CodecUtil::validateHeaderValue(input("\tabc\"\\\r\\\n\""),
                                             CodecUtil::COMPLIANT));
}

TEST(CodecUtil, validateHeaderValueDetail) {
  using Error = CodecUtil::HeaderValueError;
  const std::vector<std::pair<const char *, Error>> strictCases{
      {"abc", Error::None},
      {"abc\x01", Error::CtlChar},
      {"abc\x7f", Error::DelChar},
      {"abc\xff", Error::HighAscii},
      {"abc\r \t def", Error::BareCR},
      {"abc\r\ndef", Error::CRLFNotLWS},
      {"foo\r", Error::DanglingCRLF},
      {"foo\r\n", Error::DanglingCRLF},
  };
  for (size_t i = 0; i < strictCases.size(); i++) {
    const auto &[value, expected] = strictCases[i];
    EXPECT_EQ(
        CodecUtil::validateHeaderValueDetail(input(value), CodecUtil::STRICT),
        expected)
        << "case " << i;
  }

  // Escapes are only honored in COMPLIANT mode, so a value ending mid-escape
  // is only reachable there.
  EXPECT_EQ(
      CodecUtil::validateHeaderValueDetail(input("\"\\"), CodecUtil::COMPLIANT),
      Error::DanglingEscape);
}

// Regression guard for the plain-ASCII fast path in validateHeaderValue. The
// fast path accepts a value outright only when every byte is "plain" (HTAB or
// printable US-ASCII excluding '"' and '\\'); any other byte falls through to
// the state machine. These cases pin down the transition bytes on both sides
// of that fast/slow split in all three modes, so the fast path can never drift
// from the state machine it short-circuits. Expected values are derived from
// the RFC rules the state machine implements, not copied from the fast path.
TEST(CodecUtil, validateHeaderValueFastPathBoundaries) {
  auto check = [](const std::string &value,
                  bool compliant,
                  bool strictCompat,
                  bool strict) {
    auto range = folly::ByteRange{
        reinterpret_cast<const uint8_t *>(value.data()), value.size()};
    EXPECT_EQ(CodecUtil::validateHeaderValue(range, CodecUtil::COMPLIANT),
              compliant);
    EXPECT_EQ(CodecUtil::validateHeaderValue(range, CodecUtil::STRICT_COMPAT),
              strictCompat);
    EXPECT_EQ(CodecUtil::validateHeaderValue(range, CodecUtil::STRICT), strict);
  };

  //                                COMPLIANT STRICT_COMPAT STRICT
  // 0x1F (CTL) rejects; 0x20 (SP) is the first plain byte.
  check(std::string("a\x1f"), false, false, false);
  check(std::string("a\x20"), true, true, true);
  // 0x7E (~) is the last plain byte; 0x7F (DEL) rejects; 0x80 (obs-text) is
  // accepted except under STRICT.
  check(std::string("a\x7e"), true, true, true);
  check(std::string("a\x7f"), false, false, false);
  check(std::string("a\x80"), true, true, false);
  // 0x09 (HTAB) is plain; 0x22 (") and 0x5C (\) are the state-machine trigger
  // bytes (unterminated quote/backslash are accepted).
  check(std::string("a\x09"), true, true, true);
  check(std::string("a\""), true, true, true);
  check(std::string("a\\"), true, true, true);
  // A quoted-and-escaped CTL is accepted only in COMPLIANT mode.
  check(std::string("\"\\\x01\""), true, false, false);
  // An LWS fold (CR LF SP) is accepted in every mode.
  check(std::string("abc\r\n def"), true, true, true);
}

TEST(CodecUtil, hasGzipAndDeflate) {
  bool gzip = false;
  bool deflate = false;
  EXPECT_FALSE(CodecUtil::hasGzipAndDeflate("gzip", gzip, deflate));
  EXPECT_TRUE(gzip);
  gzip = false;
  deflate = false;
  EXPECT_FALSE(CodecUtil::hasGzipAndDeflate("deflate", gzip, deflate));
  EXPECT_TRUE(deflate);
  EXPECT_TRUE(CodecUtil::hasGzipAndDeflate("gzip, deflate", gzip, deflate));
  EXPECT_TRUE(CodecUtil::hasGzipAndDeflate("deflate, gzip", gzip, deflate));
  EXPECT_TRUE(
      CodecUtil::hasGzipAndDeflate("foo, gzip, bar, deflate", gzip, deflate));
  EXPECT_FALSE(CodecUtil::hasGzipAndDeflate("zipg, default", gzip, deflate));
  EXPECT_FALSE(
      CodecUtil::hasGzipAndDeflate("gzip; q=.00001, deflate", gzip, deflate));
}

} // namespace proxygen::test
