/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/structuredheaders/StructuredHeadersBuffer.h>
#include <string>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/structuredheaders/StructuredHeadersConstants.h>
#include <common/encode/Base64.h>

namespace proxygen{

using namespace facebook;

class StructuredHeadersBufferTest : public testing::Test {
};

TEST_F(StructuredHeadersBufferTest, test_binary_content) {
  std::string input = "*bWF4aW0gaXMgdGhlIGJlc3Q=*";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::BINARYCONTENT);
  EXPECT_EQ(output, std::string("maxim is the best"));
}

TEST_F(StructuredHeadersBufferTest, test_binary_content_illegal_characters) {
  std::string input = "*()645\t  this is not a b64 encoded string ((({]}}}))*";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_binary_content_no_ending_asterisk) {
  std::string input = "*seattle";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_binary_content_empty) {
  std::string input = "**";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::BINARYCONTENT);
  EXPECT_EQ(output, std::string(""));
}

TEST_F(StructuredHeadersBufferTest, test_identifier) {
  std::string input = "abcdefg";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseIdentifier(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::IDENTIFIER);
  EXPECT_EQ(output, std::string("abcdefg"));
}

TEST_F(StructuredHeadersBufferTest, test_identifier_all_legal_characters) {
  std::string input = "a0_-*/";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseIdentifier(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::IDENTIFIER);
  EXPECT_EQ(output, std::string("a0_-*/"));
}

TEST_F(StructuredHeadersBufferTest, test_identifier_beginning_underscore) {
  std::string input = "_af09d____****";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseIdentifier(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_string) {
  std::string input = "\"fsdfsdf\"sdfsdf\"";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::STRING);
  EXPECT_EQ(output, std::string("fsdfsdf"));
}

TEST_F(StructuredHeadersBufferTest, test_string_escaped_quote) {
  std::string input = "\"abc\\\"def\"";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::STRING);
  EXPECT_EQ(output, std::string("abc\"def"));
}

TEST_F(StructuredHeadersBufferTest, test_string_escaped_backslash) {
  std::string input = "\"abc\\\\def\"";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::STRING);
  EXPECT_EQ(output, std::string("abc\\def"));
}

TEST_F(StructuredHeadersBufferTest, test_string_stray_backslash) {
  std::string input = "\"abc\\def\"";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_string_invalid_character) {
  std::string input = "\"abcdefg\thij\"";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_string_parsing_repeated) {
  std::string input = "\"proxy\"\"gen\"";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::STRING);
  EXPECT_EQ(output, std::string("proxy"));

  err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::STRING);
  EXPECT_EQ(output, std::string("gen"));
}

TEST_F(StructuredHeadersBufferTest, test_integer) {
  std::string input = "843593";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::INT64);
  EXPECT_EQ(output, int64_t(843593));
}

TEST_F(StructuredHeadersBufferTest, test_integer_two_negatives) {
  std::string input = "--843593";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_integer_empty_after_negative) {
  std::string input = "-";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_integer_negative) {
  std::string input = "-843593";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::INT64);
  EXPECT_EQ(output, int64_t(-843593));
}

TEST_F(StructuredHeadersBufferTest, test_integer_overflow) {
  std::string input = "9223372036854775808";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_integer_high_borderline) {
  std::string input = "9223372036854775807";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::INT64);
  EXPECT_EQ(output, std::numeric_limits<int64_t>::max());
}

TEST_F(StructuredHeadersBufferTest, test_integer_low_borderline) {
  std::string input = "-9223372036854775808";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::INT64);
  EXPECT_EQ(output, std::numeric_limits<int64_t>::min());
}

TEST_F(StructuredHeadersBufferTest, test_integer_underflow) {
  std::string input = "-9223372036854775809";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_float) {
  std::string input = "3.1415926536";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::DOUBLE);
  EXPECT_EQ(output, 3.1415926536);
}

TEST_F(StructuredHeadersBufferTest, test_float_preceding_whitespace) {
  std::string input = "         \t\t    66000.5645";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::DOUBLE);
  EXPECT_EQ(output, 66000.5645);
}

TEST_F(StructuredHeadersBufferTest, test_float_no_digit_preceding_decimal) {
  std::string input = ".1415926536";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_integer_too_many_chars) {
  std::string input = "10000000000000000000"; // has 20 characters
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_float_too_many_chars) {
  std::string input = "111111111.1111111"; // has 17 characters
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_float_borderline_num_chars) {
  std::string input = "111111111.111111"; // has 16 characters
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::DOUBLE);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_float_ends_with_decimal) {
  std::string input = "100.";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  auto err = shd.parseItem(output);
  EXPECT_NE(err, StructuredHeaders::DecodeError::OK);
}

TEST_F(StructuredHeadersBufferTest, test_consume_comma) {
  std::string input = ",5345346";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  shd.removeSymbol(",", true);
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::INT64);
  EXPECT_EQ(output, int64_t(5345346));
}

TEST_F(StructuredHeadersBufferTest, test_consume_equals) {
  std::string input = "=456346.646";
  StructuredHeadersBuffer shd(input);
  StructuredHeaderItem output;
  shd.removeSymbol("=", true);
  auto err = shd.parseItem(output);
  EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
  EXPECT_EQ(output.tag, StructuredHeaderItem::Type::DOUBLE);
  EXPECT_EQ(output, 456346.646);
}

TEST_F(StructuredHeadersBufferTest, test_consume_messy) {
  std::string input = "asfgsdfg,asfgsdfg,";
  StructuredHeadersBuffer shd(input);
  for (int i = 0; i < 2; i++) {
    StructuredHeaderItem output;
    auto err = shd.parseIdentifier(output);
    EXPECT_EQ(err, StructuredHeaders::DecodeError::OK);
    EXPECT_EQ(output.tag, StructuredHeaderItem::Type::IDENTIFIER);
    EXPECT_EQ(output, std::string("asfgsdfg"));
    shd.removeSymbol(",", true);
  }
}

TEST_F(StructuredHeadersBufferTest, test_inequality_operator) {
  StructuredHeaderItem integerItem;
  integerItem.value = int64_t(999);

  StructuredHeaderItem doubleItem;
  doubleItem.value = 11.43;

  StructuredHeaderItem stringItem;
  stringItem.value = std::string("hi");

  EXPECT_NE(integerItem, int64_t(998));
  EXPECT_NE(doubleItem, double(11.44));
  EXPECT_NE(stringItem, std::string("bye"));
}

}
