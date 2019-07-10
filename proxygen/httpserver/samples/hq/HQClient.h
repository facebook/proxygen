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

#include <iostream>
#include <fstream>
#include <string>
#include <thread>

#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/json.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBaseManager.h>

#include <proxygen/httpclient/samples/curl/CurlClient.h>
#include <proxygen/httpserver/samples/hq/InsecureVerifierDangerousDoNotUseInProduction.h>
#include <proxygen/httpserver/samples/hq/PartiallyReliableCurlClient.h>
#include <proxygen/lib/http/SynchronizedLruQuicPskCache.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/session/HQUpstreamSession.h>
#include <quic/api/QuicSocket.h>
#include <quic/logging/FileQLogger.h>
#include <quic/client/QuicClientTransport.h>
#include <quic/common/Timers.h>
#include <quic/congestion_control/CongestionControllerFactory.h>

namespace quic { namespace samples {

class HQClient : private proxygen::HQSession::ConnectCallback {
 public:
  HQClient(const std::string& host,
           uint16_t port,
           const std::string& headers,
           const std::string& body,
           const std::string& path,
           const std::string& version,
           quic::TransportSettings transportSettings,
           folly::Optional<quic::QuicVersion> draftVersion,
           bool useDraftFirst,
           const std::chrono::milliseconds txnTimeout,
           const std::string& qLoggerPath,
           bool prettyJson,
           bool usePartialReliability = false,
           folly::Optional<uint64_t> prChunkDelayMs = folly::none)
      : host_(host),
        port_(port),
        body_(body),
        path_(path),
        version_(version),
        transportSettings_(transportSettings),
        draftVersion_(draftVersion),
        useDraftFirst_(useDraftFirst),
        txnTimeout_(txnTimeout),
        qLoggerPath_(qLoggerPath),
        prettyJson_(prettyJson),
        usePartialReliability_(usePartialReliability),
        prChunkDelayMs_(prChunkDelayMs) {
    headers_ = CurlService::CurlClient::parseHeaders(headers);
    if (transportSettings_.pacingEnabled) {
      pacingTimer_ = TimerHighRes::newTimer(
          &evb_, transportSettings_.pacingTimerTickInterval);
    }
  }

  void setProtocol(const std::string protocol) {
    protocol_ = protocol;
  }

  void setQuicPskCache(std::shared_ptr<quic::QuicPskCache> quicPskCache) {
    quicPskCache_ = std::move(quicPskCache);
  }

  void setEarlyData(bool earlyData) {
    earlyData_ = earlyData;
  }

  void start() {
    folly::SocketAddress addr(host_.c_str(), port_, true);

    auto sock = std::make_unique<folly::AsyncUDPSocket>(&evb_);
    quicClient_ =
        std::make_shared<quic::QuicClientTransport>(&evb_, std::move(sock));
    // TODO: turn on cert verification
    auto ctx = std::make_shared<fizz::client::FizzClientContext>();
    if (protocol_) {
      ctx->setSupportedAlpns({*protocol_});
    } else {
      ctx->setSupportedAlpns({"h1q-fb",
                              "h1q-fb-v2",
                              proxygen::kH3FBCurrentDraft,
                              proxygen::kH3CurrentDraft,
                              proxygen::kHQCurrentDraft});
    }
    ctx->setDefaultShares(
        {fizz::NamedGroup::x25519, fizz::NamedGroup::secp256r1});
    ctx->setSendEarlyData(earlyData_);
    quicClient_->setPacingTimer(pacingTimer_);
    quicClient_->setHostname(host_);
    quicClient_->setFizzClientContext(ctx);
    // This is only for testing, this should not be use in prod
    quicClient_->setCertificateVerifier(
        std::make_unique<
            proxygen::InsecureVerifierDangerousDoNotUseInProduction>());
    quicClient_->addNewPeerAddress(addr);
    quicClient_->setCongestionControllerFactory(
        std::make_shared<quic::DefaultCongestionControllerFactory>());
    quicClient_->setTransportSettings(transportSettings_);
    std::vector<quic::QuicVersion> versions;
    if (useDraftFirst_ && draftVersion_) {
      versions.push_back(*draftVersion_);
    }
    versions.push_back(quic::QuicVersion::MVFST);
    if (!useDraftFirst_ && draftVersion_) {
      versions.push_back(*draftVersion_);
    }
    quicClient_->setSupportedVersions(versions);

    if (!quicPskCache_) {
      quicPskCache_ =
          std::make_shared<proxygen::SynchronizedLruQuicPskCache>(1000);
    }
    quicClient_->setPskCache(quicPskCache_);
    if (!qLoggerPath_.empty()) {
      qLogger_ = std::make_shared<quic::FileQLogger>();
      qLogger_->dcid = quicClient_->getClientConnectionId();
      quicClient_->setQLogger(qLogger_);
    }
    wangle::TransportInfo tinfo;
    session_ = new proxygen::HQUpstreamSession(txnTimeout_,
                                               std::chrono::milliseconds(2000),
                                               nullptr, // controller
                                               tinfo,
                                               nullptr); // codecfiltercallback

    // Need this for Interop since we use HTTP0.9
    session_->setForceUpstream1_1(false);

    // TODO: this could now be moved back in the ctor
    session_->setSocket(quicClient_);
    session_->setConnectCallback(this);

    LOG(INFO) << "HQClient connecting to " << addr.describe();
    session_->startNow();
    quicClient_->start(session_);

    evb_.loop();

    if (!qLoggerPath_.empty()) {
      qLogger_->outputLogsToFile(qLoggerPath_, prettyJson_);
    }
  }

  ~HQClient() = default;

 private:
  void connectSuccess() override {
    unsigned short http_major{1};
    unsigned short http_minor{1};
    if (version_.length() == 1) {
      http_major = folly::to<unsigned short>(version_);
      http_minor = 0;
    } else {
      std::string delimiter = ".";
      std::size_t pos = version_.find(delimiter);
      if (pos == std::string::npos) {
        auto errorMsg = folly::to<std::string>("Invalid http-version string: ",
                                               version_,
                                               ", defaulting to HTTP 1.1");
        LOG(ERROR) << errorMsg;
      } else {
        std::string major = version_.substr(0, pos);
        std::string minor = version_.substr(pos + delimiter.length());
        try {
          http_major = folly::to<unsigned short>(major);
          http_minor = folly::to<unsigned short>(minor);
        } catch (const folly::ConversionError& ex) {
          auto errorMsg = folly::to<std::string>(
              "Invalid version-string: ", version_, ", defaulting to HTTP 1.1");
          LOG(ERROR) << errorMsg;
        }
      }
    }

    // Set the host header to the host IP, if not specified
    if (!headers_.exists(proxygen::HTTP_HEADER_HOST)) {
      headers_.set(proxygen::HTTP_HEADER_HOST, host_);
    }

    VLOG(10) << "http-version:" << http_major << "." << http_minor;
    std::vector<folly::StringPiece> paths;
    folly::split(',', path_, paths);
    for (const auto& path : paths) {
      proxygen::URL requestUrl(path.str(), /*secure=*/true);
      if (usePartialReliability_) {
        curls_.emplace_back(std::make_unique<PartiallyReliableCurlClient>(
            &evb_,
            (body_ == "" ? proxygen::HTTPMethod::GET
                         : proxygen::HTTPMethod::POST),
            requestUrl,
            nullptr,
            headers_,
            body_,
            false,
            http_major,
            http_minor,
            prChunkDelayMs_));
      } else {
        curls_.emplace_back(std::make_unique<CurlService::CurlClient>(
            &evb_,
            (body_ == "" ? proxygen::HTTPMethod::GET
                         : proxygen::HTTPMethod::POST),
            requestUrl,
            nullptr,
            headers_,
            body_,
            false,
            http_major,
            http_minor));
      }
      curls_.back()->setLogging(true);
      auto txn = session_->newTransaction(curls_.back().get());
      if (txn) {
        curls_.back()->sendRequest(txn);
      } else {
        LOG(ERROR) << "Failed to get transaction for path=" << path;
      }
    }
    session_->drain();
    session_->closeWhenIdle();
  }

  void onReplaySafe() override {
    VLOG(10) << "Transport replay safe";
  }

  void connectError(
      std::pair<quic::QuicErrorCode, std::string> error) override {
    auto errorMsg = folly::to<std::string>("HQClient failed to connect, error=",
                                           toString(error.first));
    LOG(ERROR) << errorMsg;
  }

 private:
  std::string host_;
  uint16_t port_;
  proxygen::HTTPHeaders headers_;
  std::string body_;
  std::shared_ptr<quic::QuicClientTransport> quicClient_;
  TimerHighRes::SharedPtr pacingTimer_;
  std::string path_;
  std::string version_;
  folly::Optional<std::string> protocol_;
  quic::TransportSettings transportSettings_;
  folly::Optional<quic::QuicVersion> draftVersion_;
  bool useDraftFirst_;
  std::chrono::milliseconds txnTimeout_;
  std::string qLoggerPath_;
  folly::EventBase evb_;
  proxygen::HQUpstreamSession* session_;
  std::list<std::unique_ptr<CurlService::CurlClient>> curls_;
  std::shared_ptr<QuicPskCache> quicPskCache_;
  bool earlyData_{false};
  bool prettyJson_{true};
  std::shared_ptr<FileQLogger> qLogger_;
  bool usePartialReliability_;
  folly::Optional<uint64_t> prChunkDelayMs_;
};
}} // namespace quic::samples
