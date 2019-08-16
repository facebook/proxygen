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

#include <string>
#include <vector>
#include <map>
#include <initializer_list>

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

// NOTE: flags are defined in HQParams.cpp
// and termporarily declared here until both HQClient
// and HQServer are transitioned to use HQParams
DECLARE_string(host);
DECLARE_int32(port);
DECLARE_int32(h2port);
DECLARE_string(mode);
DECLARE_string(body);
DECLARE_string(path);
DECLARE_string(httpversion);
DECLARE_string(protocol);
DECLARE_int32(draft_version);
DECLARE_bool(use_draft);
DECLARE_string(logdir);
DECLARE_string(congestion);
DECLARE_int32(conn_flow_control);
DECLARE_int32(stream_flow_control);
DECLARE_int32(max_receive_packet_size);
DECLARE_int32(txn_timeout);
DECLARE_string(headers);
DECLARE_bool(pacing);
DECLARE_string(psk_file);
DECLARE_bool(early_data);
DECLARE_uint32(quic_batching_mode);
DECLARE_uint32(quic_batch_size);
DECLARE_string(cert);
DECLARE_string(key);
DECLARE_string(qlogger_path);
DECLARE_bool(pretty_json);

// Partially reliable flags.
DECLARE_bool(use_pr);
DECLARE_uint32(pr_chunk_size);
DECLARE_uint32(pr_chunk_delay_ms);
// Example of starting a server streaming body in chunks in partially realible
// mode (serve 17-byte body chunks with random delay from 0 to 500 ms):
//    hq -mode server
//        -use_pr
//        -protocol="h3-20"
//        -pr_chunk_size 17
//        -pr_chunk_delay_ms 500
// Example of starting a client requesting a partial reliable streaming with
// delay cap of 150 ms:
//    hq -mode client
//        -use_pr
//        -protocol="h3-20"
//        -path="/pr_cat"
//        -pr_chunk_delay_ms 150
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

  explicit operator bool() const noexcept { return valid(); }

  const HQInvalidParams& invalidParams() const noexcept;

  // General section
  HQMode mode;
  std::string logprefix;
  std::string logdir;

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
