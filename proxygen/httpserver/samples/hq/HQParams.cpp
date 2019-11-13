/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
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
DEFINE_string(outdir, "", "Directory to store responses");
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
DEFINE_int32(pacing_timer_tick_interval_us, 200, "Pacing timer resolution");
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
DEFINE_bool(connect_udp, false, "Whether or not to use connected udp sockets");
DEFINE_uint32(max_cwnd_mss,
              quic::kLargeMaxCwndInMss,
              "Max cwnd in unit of mss");
DEFINE_string(static_root,
              "",
              "Path to serve static files from. Disabled if empty.");
DEFINE_bool(migrate_client,
            false,
            "(HQClient) Should the HQClient make two sets of requests and switch sockets in the middle.");

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
 * The pattern is to collect flags into the HQParamsBuilderFromCmdline object
 * and then to validate it. Rationale of validating the options AFTER
 * all the options have been collected: some combinations of transport,
 * http and partial reliability options are invalid. It is simpler
 * to collect the options first and to validate the combinations later.
 *
 */
void initializeCommonSettings(HQParams& hqParams) {
  // General section
  hqParams.host = FLAGS_host;
  hqParams.port = FLAGS_port;

  hqParams.logdir = FLAGS_logdir;
  hqParams.logResponse = FLAGS_log_response;
  if (FLAGS_mode == "server") {
    hqParams.mode = HQMode::SERVER;
    hqParams.logprefix = "server";
    hqParams.localAddress =
        folly::SocketAddress(hqParams.host, hqParams.port, true);
  } else if (FLAGS_mode == "client") {
    hqParams.mode = HQMode::CLIENT;
    hqParams.logprefix = "client";
    hqParams.remoteAddress =
        folly::SocketAddress(hqParams.host, hqParams.port, true);
    hqParams.outdir = FLAGS_outdir;
  }
}

void initializeTransportSettings(HQParams& hqParams) {
  // Transport section
  hqParams.quicVersions = {quic::QuicVersion::MVFST};
  if (hqParams.mode == HQMode::SERVER) {
    hqParams.quicVersions.push_back(quic::QuicVersion::MVFST_OLD);
  }
  if (FLAGS_draft_version != 0) {
    auto draftVersion =
        static_cast<quic::QuicVersion>(0xff000000 | FLAGS_draft_version);

    bool useDraftFirst = FLAGS_use_draft;
    if (useDraftFirst) {
      hqParams.quicVersions.insert(hqParams.quicVersions.begin(), draftVersion);
    } else {
      hqParams.quicVersions.push_back(draftVersion);
    }
  }

  if (!FLAGS_protocol.empty()) {
    hqParams.protocol = FLAGS_protocol;
    hqParams.supportedAlpns = {hqParams.protocol};
  } else {
    hqParams.supportedAlpns = {"h1q-fb",
                               "h1q-fb-v2",
                               proxygen::kH3FBCurrentDraft,
                               proxygen::kH3CurrentDraft,
                               proxygen::kHQCurrentDraft};
  }

  hqParams.transportSettings.advertisedInitialConnectionWindowSize =
      FLAGS_conn_flow_control;
  hqParams.transportSettings.advertisedInitialBidiLocalStreamWindowSize =
      FLAGS_stream_flow_control;
  hqParams.transportSettings.advertisedInitialBidiRemoteStreamWindowSize =
      FLAGS_stream_flow_control;
  hqParams.transportSettings.advertisedInitialUniStreamWindowSize =
      FLAGS_stream_flow_control;
  hqParams.congestionControlName = FLAGS_congestion;
  hqParams.congestionControl = flagsToCongestionControlType(FLAGS_congestion);
  if (hqParams.congestionControl) {
    hqParams.transportSettings.defaultCongestionController =
        hqParams.congestionControl.value();
  }
  hqParams.transportSettings.maxRecvPacketSize = FLAGS_max_receive_packet_size;
  hqParams.transportSettings.pacingEnabled = FLAGS_pacing;
  if (hqParams.transportSettings.pacingEnabled) {
    hqParams.transportSettings.pacingTimerTickInterval =
        std::chrono::microseconds(FLAGS_pacing_timer_tick_interval_us);
  }
  hqParams.transportSettings.batchingMode =
      quic::getQuicBatchingMode(FLAGS_quic_batching_mode);
  hqParams.transportSettings.maxBatchSize = FLAGS_quic_batch_size;
  hqParams.transportSettings.turnoffPMTUD = true;
  hqParams.transportSettings.partialReliabilityEnabled = FLAGS_use_pr;
  if (hqParams.mode == HQMode::CLIENT) {
    // There is no good reason to keep the socket around for a drain period for
    // a commandline client
    hqParams.transportSettings.shouldDrain = false;
  }
  hqParams.transportSettings.connectUDP = FLAGS_connect_udp;
  hqParams.transportSettings.maxCwndInMss = FLAGS_max_cwnd_mss;
  hqParams.transportSettings.disableMigration = false;
} // initializeTransportSettings

void initializeHttpSettings(HQParams& hqParams) {
  // HTTP section
  // NOTE: handler factories are assigned by H2Server class
  // before starting.
  hqParams.h2port = FLAGS_h2port;
  hqParams.localH2Address =
      folly::SocketAddress(hqParams.host, hqParams.h2port, true);
  hqParams.httpServerThreads = std::thread::hardware_concurrency();
  hqParams.httpServerIdleTimeout = std::chrono::milliseconds(60000);
  hqParams.httpServerShutdownOn = {SIGINT, SIGTERM};
  hqParams.httpServerEnableContentCompression = false;
  hqParams.h2cEnabled = false;
  hqParams.httpVersion.parse(FLAGS_httpversion);
  hqParams.txnTimeout = std::chrono::milliseconds(FLAGS_txn_timeout);
  folly::split(',', FLAGS_path, hqParams.httpPaths);
  hqParams.httpBody = FLAGS_body;
  hqParams.httpMethod = hqParams.httpBody.empty() ? proxygen::HTTPMethod::GET
                                                  : proxygen::HTTPMethod::POST;

  // parse HTTP headers
  hqParams.httpHeadersString = FLAGS_headers;
  hqParams.httpHeaders =
      CurlService::CurlClient::parseHeaders(hqParams.httpHeadersString);

  // Set the host header
  if (!hqParams.httpHeaders.exists(proxygen::HTTP_HEADER_HOST)) {
    hqParams.httpHeaders.set(proxygen::HTTP_HEADER_HOST, hqParams.host);
  }

  hqParams.migrateClient = FLAGS_migrate_client;

} // initializeHttpSettings

void initializePartialReliabilitySettings(HQParams& hqParams) {
  hqParams.partialReliabilityEnabled = FLAGS_use_pr;
  hqParams.prChunkSize = folly::to<uint64_t>(FLAGS_pr_chunk_size);
  // TODO: use chrono instead of uint64_t
  hqParams.prChunkDelayMs = folly::to<uint64_t>(FLAGS_pr_chunk_delay_ms);
} // initializePartialReliabilitySettings

void initializeQLogSettings(HQParams& hqParams) {
  hqParams.qLoggerPath = FLAGS_qlogger_path;
  hqParams.prettyJson = FLAGS_pretty_json;
} // initializeQLogSettings

void initializeStaticSettings(HQParams& hqParams) {

  CHECK(FLAGS_static_root.empty() || hqParams.mode == HQMode::SERVER)
      << "static_root only allowed in server mode";
  hqParams.staticRoot = FLAGS_static_root;
} // initializeStaticSettings

void initializeFizzSettings(HQParams& hqParams) {
  hqParams.earlyData = FLAGS_early_data;
  hqParams.certificateFilePath = FLAGS_cert;
  hqParams.keyFilePath = FLAGS_key;
  hqParams.pskFilePath = FLAGS_psk_file;
  if (!FLAGS_psk_file.empty()) {
    hqParams.pskCache = std::make_shared<proxygen::PersistentQuicPskCache>(
        FLAGS_psk_file,
        wangle::PersistentCacheConfig::Builder()
            .setCapacity(1000)
            .setSyncInterval(std::chrono::seconds(1))
            .build());
  } else {
    hqParams.pskCache =
        std::make_shared<proxygen::SynchronizedLruQuicPskCache>(1000);
  }

  if (FLAGS_client_auth_mode == "none") {
    hqParams.clientAuth = fizz::server::ClientAuthMode::None;
  } else if (FLAGS_client_auth_mode == "optional") {
    hqParams.clientAuth = fizz::server::ClientAuthMode::Optional;
  } else if (FLAGS_client_auth_mode == "required") {
    hqParams.clientAuth = fizz::server::ClientAuthMode::Required;
  }

} // initializeFizzSettings

HQInvalidParams validate(const HQParams& params) {

  HQInvalidParams invalidParams;
#define INVALID_PARAM(param, error)                                           \
  do {                                                                        \
    HQInvalidParam invalid = {.name = #param,                                 \
                              .value = folly::to<std::string>(FLAGS_##param), \
                              .errorMsg = error};                             \
    invalidParams.push_back(invalid);                                         \
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
  } catch (const folly::ConversionError&) {
    LOG(ERROR) << "Invalid http-version string: " << version
               << ", defaulting to HTTP/1.1";
    major = 1;
    minor = 1;
    canonical = folly::to<std::string>(major, ".", minor);
    return false;
  }
}

HQParamsBuilderFromCmdline::HQParamsBuilderFromCmdline(
    initializer_list initial) {
  // Save the values of the flags, so that changing
  // flags values is safe
  gflags::FlagSaver saver;

  for (auto& kv : initial) {
    gflags::SetCommandLineOptionWithMode(
        kv.first.c_str(),
        kv.second.c_str(),
        gflags::FlagSettingMode::SET_FLAGS_VALUE);
  }

  initializeCommonSettings(hqParams_);

  initializeTransportSettings(hqParams_);

  initializeHttpSettings(hqParams_);

  initializePartialReliabilitySettings(hqParams_);

  initializeQLogSettings(hqParams_);

  initializeFizzSettings(hqParams_);

  initializeStaticSettings(hqParams_);

  for (auto& err : validate(hqParams_)) {
    invalidParams_.push_back(err);
  }
}

bool HQParamsBuilderFromCmdline::valid() const noexcept {
  return invalidParams_.empty();
}

const HQInvalidParams& HQParamsBuilderFromCmdline::invalidParams() const
    noexcept {
  return invalidParams_;
}

HQParams HQParamsBuilderFromCmdline::build() noexcept {
  return hqParams_;
}

const folly::Expected<HQParams, HQInvalidParams> initializeParamsFromCmdline(
    HQParamsBuilderFromCmdline::initializer_list defaultValues) {
  auto builder = std::make_shared<HQParamsBuilderFromCmdline>(defaultValues);

  // Wrap up and return
  if (builder->valid()) {
    return builder->build();
  } else {
    auto errors = builder->invalidParams();
    return folly::makeUnexpected(errors);
  }
}

}} // namespace quic::samples
