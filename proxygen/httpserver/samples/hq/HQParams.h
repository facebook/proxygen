/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include <folly/Optional.h>
#include <folly/SocketAddress.h>
#include <proxygen/httpclient/samples/curl/CurlClient.h>
#include <proxygen/httpserver/HTTPServerOptions.h>
#include <proxygen/httpserver/samples/hq/HQParams.h>
#include <proxygen/lib/http/HTTPHeaders.h>
#include <proxygen/lib/http/HTTPMethod.h>
#include <quic/QuicConstants.h>
#include <quic/fizz/client/handshake/QuicPskCache.h>
#include <quic/server/QuicServerTransport.h>

namespace quic { namespace samples {

struct HTTPVersion {
  std::string version;
  std::string canonical;
  uint16_t major{1};
  uint16_t minor{1};
  bool parse(const std::string&);
};

std::ostream& operator<<(std::ostream& o, const HTTPVersion& v);

enum class HQMode { INVALID, CLIENT, SERVER };

std::ostream& operator<<(std::ostream& o, const HQMode& m);

/**
 * Struct to hold both HTTP/3 and HTTP/2 settings for HQ
 *
 * TODO: Split h2 and h3
 */
struct HQParams {
  // General section
  HQMode mode;
  std::string logprefix;
  std::string logdir;
  std::string outdir;
  bool logResponse;
  bool logResponseHeaders;

  // Transport section
  std::string host;
  uint16_t port;
  std::string protocol;
  folly::Optional<folly::SocketAddress> localAddress;
  folly::Optional<folly::SocketAddress> remoteAddress;
  std::string transportVersion;
  std::vector<quic::QuicVersion> quicVersions;
  std::vector<std::string> supportedAlpns;
  std::string localHostname;
  std::string httpProtocol;
  quic::TransportSettings transportSettings;
  std::string congestionControlName;
  folly::Optional<quic::CongestionControlType> congestionControl;
  bool earlyData;
  folly::Optional<int64_t> rateLimitPerThread;
  std::chrono::milliseconds connectTimeout;
  std::string ccpConfig;
  bool sendKnobFrame;

  // HTTP section
  uint16_t h2port;
  folly::Optional<folly::SocketAddress> localH2Address;
  HTTPVersion httpVersion;
  std::string httpHeadersString;
  proxygen::HTTPHeaders httpHeaders;
  std::string httpBody;
  proxygen::HTTPMethod httpMethod;
  std::vector<folly::StringPiece> httpPaths;

  std::chrono::milliseconds txnTimeout;

  size_t httpServerThreads;
  std::chrono::milliseconds httpServerIdleTimeout;
  std::vector<int> httpServerShutdownOn;
  bool httpServerEnableContentCompression;
  bool h2cEnabled;

  // QLogger section
  std::string qLoggerPath;
  bool prettyJson;

  // Static options
  std::string staticRoot;

  // Fizz options
  std::string certificateFilePath;
  std::string keyFilePath;
  std::string pskFilePath;
  std::shared_ptr<quic::QuicPskCache> pskCache;
  fizz::server::ClientAuthMode clientAuth{fizz::server::ClientAuthMode::None};

  // Transport knobs
  std::string transportKnobs;

  bool migrateClient{false};
};

struct HQInvalidParam {
  std::string name;
  std::string value;
  std::string errorMsg;
};

using HQInvalidParams = std::vector<HQInvalidParam>;

/**
 * A Builder class for HQParams that will build HQParams from command line
 * parameters processed by GFlag.
 */
class HQParamsBuilderFromCmdline {
 public:
  using value_type = std::map<std::string, std::string>::value_type;
  using initializer_list = std::initializer_list<value_type>;

  explicit HQParamsBuilderFromCmdline(initializer_list);

  bool valid() const noexcept;

  explicit operator bool() const noexcept {
    return valid();
  }

  const HQInvalidParams& invalidParams() const noexcept;

  HQParams build() noexcept;

 private:
  HQInvalidParams invalidParams_;
  HQParams hqParams_;
};

// Output convenience
std::ostream& operator<<(std::ostream&, HQParams&);

// Initialized the parameters from the cmdline flags
const folly::Expected<HQParams, HQInvalidParams> initializeParamsFromCmdline(
    HQParamsBuilderFromCmdline::initializer_list initial = {});

}} // namespace quic::samples
