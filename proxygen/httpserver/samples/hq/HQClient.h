/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <list>
#include <memory>
#include <proxygen/httpclient/samples/curl/CurlClient.h>
#include <proxygen/httpserver/samples/hq/HQParams.h>
#include <proxygen/lib/http/session/HQUpstreamSession.h>
#include <quic/common/Timers.h>

namespace quic {

class QuicClientTransport;
class FileQLogger;

namespace samples {

class HQClient : private proxygen::HQSession::ConnectCallback {
 public:
  explicit HQClient(const HQParams& params);

  ~HQClient() override = default;

  void start();

 private:
  proxygen::HTTPTransaction* sendRequest(const proxygen::URL& requestUrl);

  void sendRequests(bool closeSession, uint64_t numOpenableStreams);

  void sendKnobFrame(const folly::StringPiece str);

  void connectSuccess() override;

  void onReplaySafe() override;

  void connectError(std::pair<quic::QuicErrorCode, std::string> error) override;

  void initializeQuicClient();

  void initializeQLogger();

  const HQParams& params_;

  std::shared_ptr<quic::QuicClientTransport> quicClient_;

  TimerHighRes::SharedPtr pacingTimer_;

  folly::EventBase evb_;

  proxygen::HQUpstreamSession* session_;

  std::list<std::unique_ptr<CurlService::CurlClient>> curls_;

  std::deque<folly::StringPiece> httpPaths_;
};

void startClient(const HQParams& params);
} // namespace samples
} // namespace quic
