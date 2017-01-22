/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/portability/GTest.h>
#include <proxygen/lib/utils/ParseURL.h>

using proxygen::ParseURL;
using std::string;

void testParseURL(const string& url,
                  const string& expectedPath,
                  const string& expectedQuery,
                  const string& expectedHost,
                  const uint16_t expectedPort,
                  const string& expectedAuthority,
                  const bool expectedValid = true) {
  ParseURL u(url);

  if (expectedValid) {
    EXPECT_EQ(url, u.url());
    EXPECT_EQ(expectedPath, u.path());
    EXPECT_EQ(expectedQuery, u.query());
    EXPECT_EQ(expectedHost, u.host());
    EXPECT_EQ(expectedPort, u.port());
    EXPECT_EQ(expectedAuthority, u.authority());
    EXPECT_EQ(expectedValid, u.valid());
  } else {
    // invalid, do not need to test values
    EXPECT_EQ(expectedValid, u.valid());
  }
}

void testHostIsIpAddress(const string& url, const bool expected) {
  ParseURL u(url);
  EXPECT_EQ(expected, u.hostIsIPAddress());
}

TEST(ParseURL, HostNoBrackets) {
  ParseURL p("/bar");

  EXPECT_EQ("", p.host());
  EXPECT_EQ("", p.hostNoBrackets());
}

TEST(ParseURL, FullyFormedURL) {
  testParseURL("http://localhost:80/foo?bar#qqq", "/foo", "bar",
               "localhost", 80, "localhost:80");
  testParseURL("http://localhost:80/foo?bar", "/foo", "bar",
               "localhost", 80, "localhost:80");
  testParseURL("http://localhost:80/foo", "/foo", "",
               "localhost", 80, "localhost:80");
  testParseURL("http://localhost:80/",  "/", "",
               "localhost", 80, "localhost:80");
  testParseURL("http://localhost:80",  "", "",
               "localhost", 80, "localhost:80");
  testParseURL("http://localhost",  "", "",
               "localhost", 0, "localhost");
  testParseURL("http://[2401:db00:2110:3051:face:0:3f:0]/", "/", "",
               "[2401:db00:2110:3051:face:0:3f:0]", 0,
               "[2401:db00:2110:3051:face:0:3f:0]");
}

TEST(ParseURL, NoScheme) {
  testParseURL("localhost:80/foo?bar#qqq", "/foo", "bar",
               "localhost", 80, "localhost:80");
  testParseURL("localhost:80/foo?bar", "/foo", "bar",
               "localhost", 80, "localhost:80");
  testParseURL("localhost:80/foo", "/foo", "",
               "localhost", 80, "localhost:80");
  testParseURL("localhost:80/", "/", "",
               "localhost", 80, "localhost:80");
  testParseURL("localhost:80", "", "",
               "localhost", 80, "localhost:80");
  testParseURL("localhost", "", "",
               "localhost", 0, "localhost");
}

TEST(ParseURL, NoSchemeIP) {
  testParseURL("1.2.3.4:54321/foo?bar#qqq",
               "/foo", "bar", "1.2.3.4", 54321, "1.2.3.4:54321");
  testParseURL("[::1]:80/foo?bar", "/foo", "bar", "[::1]", 80, "[::1]:80");
  testParseURL("[::1]/foo?bar", "/foo", "bar", "[::1]", 0, "[::1]");
}

TEST(ParseURL, PathOnly) {
  testParseURL("/f/o/o?bar#qqq", "/f/o/o", "bar", "", 0, "");
  testParseURL("/f/o/o?bar", "/f/o/o", "bar", "", 0, "");
  testParseURL("/f/o/o", "/f/o/o", "", "", 0, "");
  testParseURL("/", "/", "", "", 0, "");
  testParseURL("?foo=bar", "", "foo=bar", "", 0, "");
  testParseURL("?#", "", "", "", 0, "");
  testParseURL("#/foo/bar", "", "", "", 0, "");
}

TEST(ParseURL, QueryIsURL) {
  testParseURL("/?ids=http://vlc.afreecodec.com/download/",
               "/", "ids=http://vlc.afreecodec.com/download/", "", 0, "");
  testParseURL("/plugins/facepile.php?href=http://www.vakan.nl/hotels",
               "/plugins/facepile.php",
               "href=http://www.vakan.nl/hotels", "", 0, "");
}

TEST(ParseURL, InvalidURL) {
  testParseURL("http://tel:198433511/", "", "", "", 0, "", false);
  testParseURL("localhost:80/foo#bar?qqq", "", "", "", 0, "", false);
  testParseURL("#?", "", "", "", 0, "", false);
  testParseURL("#?hello", "", "", "", 0, "", false);
  testParseURL("[::1/foo?bar", "", "", "", 0, "", false);
  testParseURL("", "", "", "", 0, "", false);
  testParseURL("http://tel:198433511/test\n", "", "", "", 0, "", false);
  testParseURL("/test\n", "", "", "", 0, "", false);
}

TEST(ParseURL, IsHostIPAddress) {
  testHostIsIpAddress("http://127.0.0.1:80", true);
  testHostIsIpAddress("127.0.0.1", true);
  testHostIsIpAddress("http://[::1]:80", true);
  testHostIsIpAddress("[::1]", true);
  testHostIsIpAddress("[::AB]", true);

  testHostIsIpAddress("http://localhost:80", false);
  testHostIsIpAddress("http://localhost", false);
  testHostIsIpAddress("localhost", false);
  testHostIsIpAddress("1.2.3.-1", false);
  testHostIsIpAddress("1.2.3.999", false);
  testHostIsIpAddress("::1", false);
  testHostIsIpAddress("[::99999999]", false);

  // invalid url
  testHostIsIpAddress("", false);
  testHostIsIpAddress("127.0.0.1:80/foo#bar?qqq", false);
}
