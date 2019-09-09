/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/httpserver/samples/hq/HQParams.h>

#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/net/NetworkSocket.h>
#include <folly/portability/GFlags.h>
#include <proxygen/lib/http/SynchronizedLruQuicPskCache.h>
#include <proxygen/lib/http/session/HQSession.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/transport/PersistentQuicPskCache.h>

DEFINE_string(host, "::1", "HQ server hostname/IP");
DEFINE_int32(port, 6666, "HQ server port");
DEFINE_int32(h2port, 6667, "HTTP/2 server port");
DEFINE_string(mode, "server", "Mode to run in: 'client' or 'server'");
DEFINE_string(body, "", "Filename to read from for POST requests");
DEFINE_string(path,
              "/",
              "(HQClient) url-path to send the request to, "
              "or a comma separated list of paths to fetch in parallel");
DEFINE_string(httpversion, "1.1", "HTTP version string");
DEFINE_string(protocol, "", "HQ protocol version e.g. h1q-fb or h1q-fb-v2");
DEFINE_int32(draft_version, 0, "Draft version to use, 0 is default");
DEFINE_bool(use_draft, true, "Use draft version as first version");
DEFINE_string(logdir, "/tmp/logs", "Directory to store connection logs");
DEFINE_bool(log_response,
            true,
            "Whether to log the response content to stderr");
DEFINE_string(congestion, "cubic", "newreno/cubic/bbr/none");
DEFINE_int32(conn_flow_control, 1024 * 1024, "Connection flow control");
DEFINE_int32(stream_flow_control, 65 * 1024, "Stream flow control");
DEFINE_int32(max_receive_packet_size,
             quic::kDefaultUDPReadBufferSize,
             "Max UDP packet size Quic can receive");
DEFINE_int32(txn_timeout, 120000, "HTTP Transaction Timeout");
DEFINE_string(httpauth, "", "HTTP Authority field, defaults to --host");
DEFINE_string(headers, "", "List of N=V headers separated by ,");
DEFINE_bool(pacing, false, "Whether to enable pacing on HQServer");
DEFINE_string(psk_file, "", "Cache file to use for QUIC psks");
DEFINE_bool(early_data, false, "Whether to use 0-rtt");
DEFINE_uint32(quic_batching_mode,
              static_cast<uint32_t>(quic::QuicBatchingMode::BATCHING_MODE_NONE),
              "QUIC batching mode");
DEFINE_uint32(quic_batch_size,
              quic::kDefaultQuicMaxBatchSize,
              "Maximum number of packets that can be batched in Quic");
DEFINE_string(cert, "", "Certificate file path");
DEFINE_string(key, "", "Private key file path");
DEFINE_string(client_auth_mode, "", "Client authentication mode");
DEFINE_string(qlogger_path,
              "",
              "Path to the directory where qlog files"
              "will be written. File is called <CID>.qlog");
DEFINE_bool(pretty_json, true, "Whether to use pretty json for QLogger output");

// Partially reliable flags.
DEFINE_bool(use_pr, false, "Use partial reliability");
DEFINE_uint32(pr_chunk_size,
              16,
              "Chunk size to use for partially realible server handler");
DEFINE_uint32(pr_chunk_delay_ms,
              0,
              "Max delay for the body chunks in partially reliable mode");
// Example of starting a server streaming body in chunks in partially realible
// mode (serve 17-byte body chunks with random delay from 0 to 500 ms):
//    hq -mode server -use_pr -protocol="h3-20" -pr_chunk_size 17
//    -pr_chunk_delay_ms 500
// Example of starting a client requesting a partial reliable streaming with
// delay cap of 150 ms:
//    hq -mode client -use_pr -protocol="h3-20" -path="/pr_cat"
//    -pr_chunk_delay_ms 150

namespace quic { namespace samples {

std::ostream& operator<<(std::ostream& o, const HTTPVersion& v) {
  o << "http-version=" << v.major << "/" << v.minor << " (orig=" << v.version
    << ", canonical=" << v.canonical << ")";
  return o;
}

std::ostream& operator<<(std::ostream& o, const HQMode& m) {
  o << "mode=";
  switch (m) {
    case HQMode::CLIENT:
      o << "client";
      break;
    case HQMode::SERVER:
      o << "server";
      break;
    default:
      o << "unknown (val=" << static_cast<uint32_t>(m) << ")";
  }
  return o;
}

namespace {
folly::Optional<quic::CongestionControlType> flagsToCongestionControlType(
    const std::string& congestionControlType) {
  if (congestionControlType == "cubic") {
    return quic::CongestionControlType::Cubic;
  } else if (congestionControlType == "newreno") {
    return quic::CongestionControlType::NewReno;
  } else if (congestionControlType == "bbr") {
    return quic::CongestionControlType::BBR;
  } else if (congestionControlType == "none") {
    return quic::CongestionControlType::None;
  }
  return folly::none;
}
/*
 * Initiazliation and validation functions.
 *
 * The pattern is to collect flags into the HQParamsBuilder object
 * and then to validate it. Rationale of validating the options AFTER
 * all the options have been collected: some combinations of transport,
 * http and partial reliability options are invalid. It is simpler
 * to collect the options first and to validate the combinations later.
 *
 */
void initializeCommonSettings(HQParamsBuilder& builder) {
  // General section
  builder.host = FLAGS_host;
  builder.port = FLAGS_port;

  builder.logdir = FLAGS_logdir;
  builder.logResponse = FLAGS_log_response;
  if (FLAGS_mode == "server") {
    builder.mode = HQMode::SERVER;
    builder.logprefix = "server";
    builder.localAddress =
        folly::SocketAddress(builder.host, builder.port, true);
  } else if (FLAGS_mode == "client") {
    builder.mode = HQMode::CLIENT;
    builder.logprefix = "client";
    builder.remoteAddress =
        folly::SocketAddress(builder.host, builder.port, true);
  }
}

void initializeTransportSettings(HQParamsBuilder& builder) {
  // Transport section
  builder.quicVersions = {quic::QuicVersion::MVFST};
  if (builder.mode == HQMode::SERVER) {
    builder.quicVersions.push_back(quic::QuicVersion::MVFST_OLD);
  }
  if (FLAGS_draft_version != 0) {
    auto draftVersion =
        static_cast<quic::QuicVersion>(0xff000000 | FLAGS_draft_version);

    bool useDraftFirst = FLAGS_use_draft;
    if (useDraftFirst) {
      builder.quicVersions.insert(builder.quicVersions.begin(), draftVersion);
    } else {
      builder.quicVersions.push_back(draftVersion);
    }
  }

  if (!FLAGS_protocol.empty()) {
    builder.protocol = FLAGS_protocol;
    builder.supportedAlpns = {builder.protocol};
  } else {
    builder.supportedAlpns = {"h1q-fb",
                              "h1q-fb-v2",
                              proxygen::kH3FBCurrentDraft,
                              proxygen::kH3CurrentDraft,
                              proxygen::kHQCurrentDraft};
  }

  builder.transportSettings.advertisedInitialConnectionWindowSize =
      FLAGS_conn_flow_control;
  builder.transportSettings.advertisedInitialBidiLocalStreamWindowSize =
      FLAGS_stream_flow_control;
  builder.transportSettings.advertisedInitialBidiRemoteStreamWindowSize =
      FLAGS_stream_flow_control;
  builder.transportSettings.advertisedInitialUniStreamWindowSize =
      FLAGS_stream_flow_control;
  builder.congestionControlName = FLAGS_congestion;
  builder.congestionControl = flagsToCongestionControlType(FLAGS_congestion);
  if (builder.congestionControl) {
    builder.transportSettings.defaultCongestionController =
        builder.congestionControl.value();
  }
  builder.transportSettings.maxRecvPacketSize = FLAGS_max_receive_packet_size;
  builder.transportSettings.pacingEnabled = FLAGS_pacing;
  builder.transportSettings.batchingMode =
      quic::getQuicBatchingMode(FLAGS_quic_batching_mode);
  builder.transportSettings.maxBatchSize = FLAGS_quic_batch_size;
  builder.transportSettings.turnoffPMTUD = true;
  builder.transportSettings.partialReliabilityEnabled = FLAGS_use_pr;
  if (builder.mode == HQMode::CLIENT) {
    // There is no good reason to keep the socket around for a drain period for
    // a commandline client
    builder.transportSettings.shouldDrain = false;
  }

} // initializeTransportSettings

void initializeHttpSettings(HQParamsBuilder& builder) {
  // HTTP section
  // NOTE: handler factories are assigned by H2Server class
  // before starting.
  builder.h2port = FLAGS_h2port;
  builder.localH2Address =
      folly::SocketAddress(builder.host, builder.h2port, true);
  builder.httpServerThreads = 1;
  builder.httpServerIdleTimeout = std::chrono::milliseconds(60000);
  builder.httpServerShutdownOn = {SIGINT, SIGTERM};
  builder.httpServerEnableContentCompression = false;
  builder.h2cEnabled = false;
  builder.httpVersion.parse(FLAGS_httpversion);
  builder.txnTimeout = std::chrono::milliseconds(FLAGS_txn_timeout);
  folly::split(',', FLAGS_path, builder.httpPaths);
  builder.httpBody = FLAGS_body;
  builder.httpMethod = builder.httpBody.empty() ? proxygen::HTTPMethod::GET
                                                : proxygen::HTTPMethod::POST;

  // parse HTTP headers
  builder.httpHeadersString = FLAGS_headers;
  builder.httpHeaders =
      CurlService::CurlClient::parseHeaders(builder.httpHeadersString);

  // Set the host header
  if (!builder.httpHeaders.exists(proxygen::HTTP_HEADER_HOST)) {
    builder.httpHeaders.set(proxygen::HTTP_HEADER_HOST, builder.host);
  }

} // initializeHttpSettings

void initializePartialReliabilitySettings(HQParamsBuilder& builder) {
  builder.partialReliabilityEnabled = FLAGS_use_pr;
  builder.prChunkSize = folly::to<uint64_t>(FLAGS_pr_chunk_size);
  // TODO: use chrono instead of uint64_t
  builder.prChunkDelayMs = folly::to<uint64_t>(FLAGS_pr_chunk_delay_ms);
} // initializePartialReliabilitySettings

void initializeQLogSettings(HQParamsBuilder& builder) {
  builder.qLoggerPath = FLAGS_qlogger_path;
  builder.prettyJson = FLAGS_pretty_json;
} // initializeQLogSettings

void initializeFizzSettings(HQParamsBuilder& builder) {
  builder.earlyData = FLAGS_early_data;
  builder.certificateFilePath = FLAGS_cert;
  builder.keyFilePath = FLAGS_key;
  builder.pskFilePath = FLAGS_psk_file;
  if (!FLAGS_psk_file.empty()) {
    builder.pskCache = std::make_shared<proxygen::PersistentQuicPskCache>(
        FLAGS_psk_file,
        wangle::PersistentCacheConfig::Builder()
            .setCapacity(1000)
            .setSyncInterval(std::chrono::seconds(1))
            .build());
  } else {
    builder.pskCache =
        std::make_shared<proxygen::SynchronizedLruQuicPskCache>(1000);
  }

  if (FLAGS_client_auth_mode == "none") {
    builder.clientAuth = fizz::server::ClientAuthMode::None;
  } else if (FLAGS_client_auth_mode == "optional") {
    builder.clientAuth = fizz::server::ClientAuthMode::Optional;
  } else if (FLAGS_client_auth_mode == "required") {
    builder.clientAuth = fizz::server::ClientAuthMode::Required;
  }

} // initializeFizzSettings

HQParamsBuilder::HQInvalidParams validate(const HQParamsBuilder& params) {

  HQParamsBuilder::HQInvalidParams invalidParams;
#define INVALID_PARAM(param, error)                     \
  do {                                                  \
    HQParamsBuilder::HQInvalidParam invalid = {         \
        .name = #param,                                 \
        .value = folly::to<std::string>(FLAGS_##param), \
        .errorMsg = error};                             \
    invalidParams.push_back(invalid);                   \
  } while (false);

  // Validate the common settings
  if (!(params.mode == HQMode::CLIENT || params.mode == HQMode::SERVER)) {
    INVALID_PARAM(mode, "only client/server are supported");
  }

  // In the client mode, host/port are required
  if (params.mode == HQMode::CLIENT) {
    if (params.host.empty()) {
      INVALID_PARAM(host, "HQClient expected --host");
    }
    if (params.port == 0) {
      INVALID_PARAM(port, "HQClient expected --port");
    }
  }

  // Validate the transport section
  if (folly::to<uint16_t>(FLAGS_max_receive_packet_size) <
      quic::kDefaultUDPSendPacketLen) {
    INVALID_PARAM(
        max_receive_packet_size,
        folly::to<std::string>("max_receive_packet_size needs to be at least ",
                               quic::kDefaultUDPSendPacketLen));
  }

  if (!params.congestionControlName.empty()) {
    if (!params.congestionControl) {
      INVALID_PARAM(congestion, "unrecognized congestion control");
    }
  }
  // Validate the HTTP section
  if (params.mode == HQMode::SERVER) {
    if (!params.httpBody.empty()) {
      INVALID_PARAM(body, "the 'body' argument is allowed only in client mode");
    }
  }

  return invalidParams;
#undef INVALID_PARAM
}
} // namespace

bool HTTPVersion::parse(const std::string& verString) {
  // version, major and minor are fields of struct HTTPVersion
  version = verString;
  if (version.length() == 1) {
    major = folly::to<uint16_t>(version);
    minor = 0;
    canonical = folly::to<std::string>(major, ".", minor);
    return true;
  }
  std::string delimiter = ".";
  std::size_t pos = version.find(delimiter);
  if (pos == std::string::npos) {
    LOG(ERROR) << "Invalid http-version string: " << version
               << ", defaulting to HTTP/1.1";
    major = 1;
    minor = 1;
    canonical = folly::to<std::string>(major, ".", minor);
    return false;
  }

  try {
    std::string majorVer = version.substr(0, pos);
    std::string minorVer = version.substr(pos + delimiter.length());
    major = folly::to<uint16_t>(majorVer);
    minor = folly::to<uint16_t>(minorVer);
    canonical = folly::to<std::string>(major, ".", minor);
    return true;
  } catch (const folly::ConversionError& ex) {
    LOG(ERROR) << "Invalid http-version string: " << version
               << ", defaulting to HTTP/1.1";
    major = 1;
    minor = 1;
    canonical = folly::to<std::string>(major, ".", minor);
    return false;
  }
}

HQParamsBuilder::HQParamsBuilder(initializer_list initial) {
  // Save the values of the flags, so that changing
  // flags values is safe
  gflags::FlagSaver saver;

  for (auto& kv : initial) {
    gflags::SetCommandLineOptionWithMode(
        kv.first.c_str(),
        kv.second.c_str(),
        gflags::FlagSettingMode::SET_FLAGS_VALUE);
  }

  initializeCommonSettings(*this);

  initializeTransportSettings(*this);

  initializeHttpSettings(*this);

  initializePartialReliabilitySettings(*this);

  initializeQLogSettings(*this);

  initializeFizzSettings(*this);

  for (auto& err : validate(*this)) {
    invalidParams_.push_back(err);
  }
}

bool HQParamsBuilder::valid() const noexcept {
  return invalidParams_.empty();
}

const HQParamsBuilder::HQInvalidParams& HQParamsBuilder::invalidParams() const
    noexcept {
  return invalidParams_;
}

const folly::Expected<HQParams, HQParamsBuilder::HQInvalidParams>
initializeParams(HQParamsBuilder::initializer_list defaultValues) {
  auto builder = std::make_shared<HQParamsBuilder>(defaultValues);

  // Wrap up and return
  if (builder->valid()) {
    return std::move(builder);
  } else {
    auto errors = builder->invalidParams();
    return folly::makeUnexpected(errors);
  }
}

}} // namespace quic::samples
