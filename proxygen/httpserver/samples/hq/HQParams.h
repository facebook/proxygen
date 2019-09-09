/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
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
#include <quic/client/handshake/QuicPskCache.h>
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
 * Used to pass common settings between different components
 */
class HQParamsBuilder {
 public:
  using value_type = std::map<std::string, std::string>::value_type;
  using initializer_list = std::initializer_list<value_type>;

  struct HQInvalidParam {
    std::string name;
    std::string value;
    std::string errorMsg;
  };

  using HQInvalidParams = std::vector<HQInvalidParam>;

  HQParamsBuilder();

  explicit HQParamsBuilder(initializer_list);

  bool valid() const noexcept;

  explicit operator bool() const noexcept {
    return valid();
  }

  const HQInvalidParams& invalidParams() const noexcept;

  // General section
  HQMode mode;
  std::string logprefix;
  std::string logdir;
  bool logResponse;

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

  // Partial reliability section
  bool partialReliabilityEnabled{false};
  folly::Optional<uint64_t> prChunkSize;
  folly::Optional<uint64_t> prChunkDelayMs;

  // QLogger section
  std::string qLoggerPath;
  bool prettyJson;

  // Fizz options
  std::string certificateFilePath;
  std::string keyFilePath;
  std::string pskFilePath;
  std::shared_ptr<quic::QuicPskCache> pskCache;
  fizz::server::ClientAuthMode clientAuth{fizz::server::ClientAuthMode::None};

 private:
  HQInvalidParams invalidParams_;
};

// Externally only the frozen type is used
using HQParams = std::shared_ptr<HQParamsBuilder>;

// Output convenience
std::ostream& operator<<(std::ostream&, HQParams&);

// Initialized the parameters from the cmdline flags
const folly::Expected<HQParams, HQParamsBuilder::HQInvalidParams>
initializeParams(HQParamsBuilder::initializer_list initial = {});

}} // namespace quic::samples
