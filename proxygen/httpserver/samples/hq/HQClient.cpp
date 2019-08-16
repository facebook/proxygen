#include <proxygen/httpserver/samples/hq/HQClient.h>

#include <fstream>
#include <ostream>
#include <string>
#include <thread>

#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/json.h>

#include <proxygen/httpserver/samples/hq/FizzContext.h>
#include <proxygen/httpserver/samples/hq/HQLoggerHelper.h>
#include <proxygen/httpserver/samples/hq/InsecureVerifierDangerousDoNotUseInProduction.h>
#include <proxygen/httpserver/samples/hq/PartiallyReliableCurlClient.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <quic/api/QuicSocket.h>
#include <quic/client/QuicClientTransport.h>
#include <quic/congestion_control/CongestionControllerFactory.h>
#include <quic/logging/FileQLogger.h>

namespace quic { namespace samples {

HQClient::HQClient(const HQParams& params) : params_(params) {
  if (params->transportSettings.pacingEnabled) {
    pacingTimer_ = TimerHighRes::newTimer(
        &evb_, params->transportSettings.pacingTimerTickInterval);
  }
}

void HQClient::start() {

  initializeQuicClient();
  initializeQLogger();

  // TODO: turn on cert verification
  wangle::TransportInfo tinfo;
  session_ = new proxygen::HQUpstreamSession(params_->txnTimeout,
                                             std::chrono::milliseconds(2000),
                                             nullptr, // controller
                                             tinfo,
                                             nullptr); // codecfiltercallback

  // Need this for Interop since we use HTTP0.9
  session_->setForceUpstream1_1(false);

  // TODO: this could now be moved back in the ctor
  session_->setSocket(quicClient_);
  session_->setConnectCallback(this);

  LOG(INFO) << "HQClient connecting to " << params_->remoteAddress->describe();
  session_->startNow();
  quicClient_->start(session_);

  evb_.loop();

}

proxygen::HTTPTransaction* FOLLY_NULLABLE HQClient::sendRequest(
    const proxygen::URL& requestUrl) {
  std::unique_ptr<CurlService::CurlClient> client =
      params_->partialReliabilityEnabled
          ? std::make_unique<PartiallyReliableCurlClient>(
                &evb_,
                params_->httpMethod,
                requestUrl,
                nullptr,
                params_->httpHeaders,
                params_->httpBody,
                false,
                params_->httpVersion.major,
                params_->httpVersion.minor,
                params_->prChunkDelayMs)
          : std::make_unique<CurlService::CurlClient>(
                &evb_,
                params_->httpMethod,
                requestUrl,
                nullptr,
                params_->httpHeaders,
                params_->httpBody,
                false,
                params_->httpVersion.major,
                params_->httpVersion.minor);

  client->setLogging(true);
  auto txn = session_->newTransaction(client.get());
  if (!txn) {
    return nullptr;
  }
  client->sendRequest(txn);
  curls_.emplace_back(std::move(client));
  return txn;
}

void HQClient::connectSuccess() {
  VLOG(10) << "http-version:" << params_->httpVersion;
  for (const auto& path : params_->httpPaths) {
    proxygen::URL requestUrl(path.str(), /*secure=*/true);
    sendRequest(requestUrl);
  }
  session_->drain();
  session_->closeWhenIdle();
}

void HQClient::onReplaySafe() {
  VLOG(10) << "Transport replay safe";
}

void HQClient::connectError(std::pair<quic::QuicErrorCode, std::string> error) {
  LOG(ERROR) << "HQClient failed to connect, error=" << toString(error.first)
             << ", msg=" << error.second;
}

void HQClient::initializeQuicClient() {
  auto sock = std::make_unique<folly::AsyncUDPSocket>(&evb_);
  auto client =
      std::make_shared<quic::QuicClientTransport>(&evb_, std::move(sock));

  client->setPacingTimer(pacingTimer_);
  client->setHostname(params_->host);
  client->setFizzClientContext(createFizzClientContext(params_));
  // This is only for testing, this should not be use in prod
  client->setCertificateVerifier(
      std::make_unique<
          proxygen::InsecureVerifierDangerousDoNotUseInProduction>());
  client->addNewPeerAddress(params_->remoteAddress.value());
  client->setCongestionControllerFactory(
      std::make_shared<quic::DefaultCongestionControllerFactory>());
  client->setTransportSettings(params_->transportSettings);
  client->setSupportedVersions(params_->quicVersions);

  client->setPskCache(params_->pskCache);
  quicClient_ = std::move(client);
}

void HQClient::initializeQLogger() {
  if (!quicClient_) {
    return;
  }
  // Not used immediately, but if not set
  // the qlogger wont be able to report. Checking early
  if (params_->qLoggerPath.empty()) {
    return;
  }

  auto qLogger = std::make_shared<HQLoggerHelper>(params_->qLoggerPath,
                                                  params_->prettyJson);
  qLogger->dcid = quicClient_->getClientConnectionId();
  quicClient_->setQLogger(std::move(qLogger));
}

void startClient(const HQParams& params) {
  HQClient client(params);
  client.start();
}

}} // namespace quic::samples
