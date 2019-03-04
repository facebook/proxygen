/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPDefaultSessionCodecFactory.h>
#include <folly/portability/GTest.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/HTTP2Codec.h>
#include <proxygen/lib/http/codec/HTTP2Constants.h>
#include <proxygen/lib/http/codec/SPDYCodec.h>
#include <proxygen/lib/services/AcceptorConfiguration.h>

using namespace proxygen;
using namespace testing;

TEST(HTTPDefaultSessionCodecFactoryTest, GetCodecSPDY) {
  AcceptorConfiguration conf;
  // If set directly on the acceptor, we should always return the SPDY version.
  conf.plaintextProtocol = "spdy/3.1";

  HTTPDefaultSessionCodecFactory factory(conf);
  auto codec = factory.getCodec(
      "http/1.1", TransportDirection::UPSTREAM, false /* isTLS */);
  SPDYCodec* spdyCodec = dynamic_cast<SPDYCodec*>(codec.get());
  EXPECT_NE(spdyCodec, nullptr);
  EXPECT_EQ(spdyCodec->getProtocol(), CodecProtocol::SPDY_3_1);

  codec = factory.getCodec(
      "spdy/3", TransportDirection::UPSTREAM, false /* isTLS */);
  spdyCodec = dynamic_cast<SPDYCodec*>(codec.get());
  EXPECT_NE(spdyCodec, nullptr);
  EXPECT_EQ(spdyCodec->getProtocol(), CodecProtocol::SPDY_3_1);

  conf.plaintextProtocol = "spdy/3";

  HTTPDefaultSessionCodecFactory secondFactory(conf);
  codec = secondFactory.getCodec(
      "http/1.1", TransportDirection::UPSTREAM, false /* isTLS */);
  spdyCodec = dynamic_cast<SPDYCodec*>(codec.get());
  EXPECT_NE(spdyCodec, nullptr);
  EXPECT_EQ(spdyCodec->getProtocol(), CodecProtocol::SPDY_3);

  codec = secondFactory.getCodec(
      "spdy/3.1", TransportDirection::UPSTREAM, false /* isTLS */);
  spdyCodec = dynamic_cast<SPDYCodec*>(codec.get());
  EXPECT_NE(spdyCodec, nullptr);
  EXPECT_EQ(spdyCodec->getProtocol(), CodecProtocol::SPDY_3);

  // On a somewhat contrived example, if TLS we should return the version
  // negotiated through ALPN.
  codec = secondFactory.getCodec(
      "h2", TransportDirection::DOWNSTREAM, true /* isTLS */);
  HTTP2Codec* httpCodec = dynamic_cast<HTTP2Codec*>(codec.get());
  EXPECT_NE(httpCodec, nullptr);
  EXPECT_EQ(httpCodec->getProtocol(), CodecProtocol::HTTP_2);
}

TEST(HTTPDefaultSessionCodecFactoryTest, GetCodecH2) {
  AcceptorConfiguration conf;
  // If set directly on the acceptor, we should always return the H2C version.
  conf.plaintextProtocol = "h2c";
  HTTPDefaultSessionCodecFactory factory(conf);
  auto codec = factory.getCodec(
      "http/1.1", TransportDirection::DOWNSTREAM, false /* isTLS */);
  HTTP2Codec* httpCodec = dynamic_cast<HTTP2Codec*>(codec.get());
  EXPECT_NE(httpCodec, nullptr);
  EXPECT_EQ(httpCodec->getProtocol(), CodecProtocol::HTTP_2);

  // On a somewhat contrived example, if TLS we should return the version
  // negotiated through ALPN.
  codec = factory.getCodec(
      "http/1.1", TransportDirection::UPSTREAM, true /* isTLS */);
  HTTP1xCodec* http1Codec = dynamic_cast<HTTP1xCodec*>(codec.get());
  EXPECT_NE(http1Codec, nullptr);
  EXPECT_EQ(http1Codec->getProtocol(), CodecProtocol::HTTP_1_1);
}

TEST(HTTPDefaultSessionCodecFactoryTest, GetCodecUpgradeProtocols) {
  std::list<std::string> plainTextUpgrades = {http2::kProtocolCleartextString};
  AcceptorConfiguration conf;
  conf.allowedPlaintextUpgradeProtocols = plainTextUpgrades;
  HTTPDefaultSessionCodecFactory factory(conf);

  auto codec =
      factory.getCodec("http/1.1", TransportDirection::DOWNSTREAM, false);
  HTTP1xCodec* downstreamCodec = dynamic_cast<HTTP1xCodec*>(codec.get());
  EXPECT_NE(downstreamCodec, nullptr);
  EXPECT_FALSE(downstreamCodec->getAllowedUpgradeProtocols().empty());
  EXPECT_EQ(downstreamCodec->getAllowedUpgradeProtocols(),
            http2::kProtocolCleartextString);

  // If TLS, we should not attempt to upgrade.
  codec = factory.getCodec("http/1.1", TransportDirection::DOWNSTREAM, true);
  downstreamCodec = dynamic_cast<HTTP1xCodec*>(codec.get());
  EXPECT_NE(downstreamCodec, nullptr);
  EXPECT_TRUE(downstreamCodec->getAllowedUpgradeProtocols().empty());
}

TEST(HTTPDefaultSessionCodecFactoryTest, GetCodec) {
  AcceptorConfiguration conf;
  HTTPDefaultSessionCodecFactory factory(conf);

  // Empty protocol should default to http/1.1
  auto codec =
      factory.getCodec("", TransportDirection::DOWNSTREAM, false /* isTLS */);
  HTTP1xCodec* http1Codec = dynamic_cast<HTTP1xCodec*>(codec.get());
  EXPECT_NE(http1Codec, nullptr);
  EXPECT_EQ(http1Codec->getProtocol(), CodecProtocol::HTTP_1_1);

  codec = factory.getCodec(
      "spdy/3.1", TransportDirection::UPSTREAM, false /* isTLS */);
  SPDYCodec* spdyCodec = dynamic_cast<SPDYCodec*>(codec.get());
  EXPECT_NE(spdyCodec, nullptr);
  EXPECT_EQ(spdyCodec->getProtocol(), CodecProtocol::SPDY_3_1);

  codec =
      factory.getCodec("h2", TransportDirection::DOWNSTREAM, false /* isTLS */);
  HTTP2Codec* httpCodec = dynamic_cast<HTTP2Codec*>(codec.get());
  EXPECT_NE(httpCodec, nullptr);
  EXPECT_EQ(httpCodec->getProtocol(), CodecProtocol::HTTP_2);

  // Not supported protocols should return nullptr.
  codec = factory.getCodec(
      "not/supported", TransportDirection::DOWNSTREAM, false /* isTLS */);
  EXPECT_EQ(codec, nullptr);
}
