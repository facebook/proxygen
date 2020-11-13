/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <algorithm>
#include <climits>
#include <functional>
#include <math.h>
#include <mutex>
#include <random>
#include <vector>

#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/Random.h>
#include <folly/ThreadLocal.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBaseManager.h>
#include <proxygen/httpserver/samples/hq/HQParams.h>
#include <proxygen/httpserver/samples/hq/PartiallyReliableCurlClient.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>

namespace quic { namespace samples {

using random_bytes_engine =
    std::independent_bits_engine<std::default_random_engine,
                                 CHAR_BIT,
                                 unsigned char>;

constexpr folly::StringPiece kPartiallyReliableScriptHeader{"x-pr-script"};
constexpr folly::StringPiece kPartiallyReliableChunkDelayMsHeader{
    "x-pr-chunk-delay-ms"};
constexpr folly::StringPiece kPartiallyReliableChunkSizeHeader{
    "x-pr-chunk-size"};
constexpr folly::StringPiece kPartiallyReliableChunkDelayCapMsHeader{
    "x-pr-chunk-delay-cap-ms"};

const uint64_t kDefaultPartiallyReliableChunkSize = 16;
const uint64_t kDefaultPartiallyReliableChunkDelayMs = 0;

class BaseSampleHandler : public proxygen::HTTPTransactionHandler {
 public:
  BaseSampleHandler() = delete;

  explicit BaseSampleHandler(const HQParams& params) : params_(params) {
  }

  void setTransaction(proxygen::HTTPTransaction* txn) noexcept override {
    txn_ = txn;
  }

  void detachTransaction() noexcept override {
    delete this;
  }

  void onChunkHeader(size_t /*length*/) noexcept override {
  }

  void onChunkComplete() noexcept override {
  }

  void onTrailers(
      std::unique_ptr<proxygen::HTTPHeaders> /*trailers*/) noexcept override {
  }

  void onUpgrade(proxygen::UpgradeProtocol /*protocol*/) noexcept override {
  }

  void onEgressPaused() noexcept override {
  }

  void onEgressResumed() noexcept override {
  }

  void maybeAddAltSvcHeader(proxygen::HTTPMessage& msg) const {
    if (params_.protocol.empty() || params_.port == 0) {
      return;
    }
    msg.getHeaders().add(
        proxygen::HTTP_HEADER_ALT_SVC,
        folly::format("{}=\":{}\"; ma=3600", params_.protocol, params_.port)
            .str());
  }

  // clang-format off
  static const std::string& getH1QFooter() {
    static const std::string footer(
" __    __  .___________.___________..______      ___ ___       ___    ______\n"
"|  |  |  | |           |           ||   _  \\    /  // _ \\     / _ \\  |      \\\n"
"|  |__|  | `---|  |----`---|  |----`|  |_)  |  /  /| | | |   | (_) | `----)  |\n"
"|   __   |     |  |        |  |     |   ___/  /  / | | | |    \\__, |     /  /\n"
"|  |  |  |     |  |        |  |     |  |     /  /  | |_| |  __  / /     |__|\n"
"|__|  |__|     |__|        |__|     | _|    /__/    \\___/  (__)/_/       __\n"
"                                                                        (__)\n"
"\n"
"\n"
"____    __    ____  __    __       ___   .___________.\n"
"\\   \\  /  \\  /   / |  |  |  |     /   \\  |           |\n"
" \\   \\/    \\/   /  |  |__|  |    /  ^  \\ `---|  |----`\n"
"  \\            /   |   __   |   /  /_\\  \\    |  |\n"
"   \\    /\\    /    |  |  |  |  /  _____  \\   |  |\n"
"    \\__/  \\__/     |__|  |__| /__/     \\__\\  |__|\n"
"\n"
"____    ____  _______     ___      .______\n"
"\\   \\  /   / |   ____|   /   \\     |   _  \\\n"
" \\   \\/   /  |  |__     /  ^  \\    |  |_)  |\n"
"  \\_    _/   |   __|   /  /_\\  \\   |      /\n"
"    |  |     |  |____ /  _____  \\  |  |\\  \\----.\n"
"    |__|     |_______/__/     \\__\\ | _| `._____|\n"
"\n"
" __       _______.    __  .___________.______\n"
"|  |     /       |   |  | |           |      \\\n"
"|  |    |   (----`   |  | `---|  |----`----)  |\n"
"|  |     \\   \\       |  |     |  |        /  /\n"
"|  | .----)   |      |  |     |  |       |__|\n"
"|__| |_______/       |__|     |__|        __\n"
"                                         (__)\n"
    );
    // clang-format on
    return footer;
  }

 protected:
  const std::string& getHttpVersion() const {
    return params_.httpVersion.canonical;
  }

  proxygen::HTTPTransaction* txn_{nullptr};
  const HQParams& params_;
};

class EchoHandler : public BaseSampleHandler {
 public:
  explicit EchoHandler(const HQParams& params) : BaseSampleHandler(params) {
  }

  EchoHandler() = delete;

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    VLOG(10) << "EchoHandler::onHeadersComplete";
    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << getHttpVersion();
    sendFooter_ =
        (msg->getHTTPVersion() == proxygen::HTTPMessage::kHTTPVersion09);
    resp.setVersionString(getHttpVersion());
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
    msg->getHeaders().forEach(
        [&](const std::string& header, const std::string& val) {
          resp.getHeaders().add(folly::to<std::string>("x-echo-", header), val);
        });
    resp.setWantsKeepalive(true);
    maybeAddAltSvcHeader(resp);
    txn_->sendHeaders(resp);
  }

  void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override {
    VLOG(10) << "EchoHandler::onBody";
    txn_->sendBody(std::move(chain));
  }

  void onEOM() noexcept override {
    VLOG(10) << "EchoHandler::onEOM";
    if (sendFooter_) {
      auto& footer = getH1QFooter();
      txn_->sendBody(folly::IOBuf::copyBuffer(footer.data(), footer.length()));
    }
    txn_->sendEOM();
  }

  void onError(const proxygen::HTTPException& /*error*/) noexcept override {
    txn_->sendAbort();
  }

 private:
  bool sendFooter_{false};
};

class TransportCallbackBase
    : public proxygen::HTTPTransactionTransportCallback {
  void firstHeaderByteFlushed() noexcept override {
  }

  void firstByteFlushed() noexcept override {
  }

  void lastByteFlushed() noexcept override {
  }

  void trackedByteFlushed() noexcept override {
  }

  void lastByteAcked(
      std::chrono::milliseconds /* latency */) noexcept override {
  }

  void headerBytesGenerated(
      proxygen::HTTPHeaderSize& /* size */) noexcept override {
  }

  void headerBytesReceived(
      const proxygen::HTTPHeaderSize& /* size */) noexcept override {
  }

  void bodyBytesGenerated(size_t /* nbytes */) noexcept override {
  }

  void bodyBytesReceived(size_t /* size */) noexcept override {
  }
};

/**
 * Streams ASCII cat in partially realible mode.
 */
class PrCatHandler
    : public EchoHandler
    , public TransportCallbackBase
    , public folly::AsyncTimeout {
 public:
  explicit PrCatHandler(const HQParams& params)
      : EchoHandler(params),
        chunkSize_(params.prChunkSize),
        chunkDelayMs_(params.prChunkDelayMs) {
  }

  PrCatHandler() = delete;

  void setPrParams(folly::Optional<uint64_t> prChunkSize,
                   folly::Optional<uint64_t> prChunkDelayMs) {
    chunkSize_ = prChunkSize;
    chunkDelayMs_ = prChunkDelayMs;
  }

  // TransportCallbackBase
  void lastEgressHeaderByteAcked() noexcept override {
    attachEventBase(folly::EventBaseManager::get()->getEventBase());
    timeoutExpired();
  }

  void onEOM() noexcept override {
  }

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> /* msg */) noexcept override {
    VLOG(10) << "PrCatHandler::onHeadersComplete";
    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << getHttpVersion();
    resp.setVersionString(getHttpVersion());
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
    resp.getHeaders().add("pr-chunk-size", folly::to<std::string>(*chunkSize_));
    resp.setWantsKeepalive(true);
    resp.setPartiallyReliable();
    txn_->setTransportCallback(this);
    txn_->sendHeaders(resp);
  }

  void onBodyPeek(uint64_t offset,
                  const folly::IOBuf& chain) noexcept override {
    LOG(INFO) << __func__ << ": got " << chain.computeChainDataLength()
              << " bytes at offset " << offset;
  }

  void onBodySkipped(uint64_t /* newOffset */) noexcept override {
    LOG(FATAL) << __func__ << ": wrong side to receive this callback ";
  }

  void onBodyRejected(uint64_t offset) noexcept override {
    cancelTimeout();
    LOG(INFO) << __func__ << ": data for chunk " << curPartNum_
              << " (offset = " << offset << ") cancelled ( was scheduled with "
              << delayMs_ << "ms delay)";

    auto chunk = dataSndr_.generateChunk();
    CHECK(chunk);
    bool eom = !dataSndr_.hasMoreData();

    if (eom) {
      LOG(INFO) << __func__ << ":    sending EOM after last chunk "
                << curPartNum_;
      txn_->sendEOM();
    } else {
      curPartNum_ = offset / *chunkSize_;
      delayMs_ = folly::Random::rand64(*chunkDelayMs_);
      scheduleTimeout(delayMs_);
    }
  }

  void timeoutExpired() noexcept override {
    LOG(INFO) << ": sending body part " << curPartNum_ << " with delay "
              << delayMs_ << " ms";
    auto chunk = dataSndr_.generateChunk();
    CHECK(chunk);
    bool eom = !dataSndr_.hasMoreData();

    txn_->sendBody(std::move(*chunk));
    if (eom) {
      LOG(INFO) << __func__ << ":    sending EOM with chunk " << curPartNum_;
      txn_->sendEOM();
    } else {
      delayMs_ = folly::Random::rand64(*chunkDelayMs_);
      scheduleTimeout(delayMs_);
    }

    curPartNum_++;
  }

 private:
  uint64_t delayMs_{0};
  uint64_t curPartNum_{0};
  folly::Optional<uint64_t> chunkSize_{kDefaultPartiallyReliableChunkSize};
  folly::Optional<uint64_t> chunkDelayMs_{
      kDefaultPartiallyReliableChunkDelayMs};
  PartiallyReliableSender dataSndr_{kDefaultPartiallyReliableChunkSize};
};

/**
 * Sends body/skip sequence according to the script received in client headers.
 */
class PrSkipHandler
    : public EchoHandler
    , public folly::AsyncTimeout {
 public:
  explicit PrSkipHandler(const HQParams& params) : EchoHandler(params) {
  }

  PrSkipHandler() = delete;

  void onEOM() noexcept override {
  }

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {

    msg->getHeaders().forEach(
        [&](const std::string& header, const std::string& val) {
          if (header == kPartiallyReliableScriptHeader) {
            script = val;
          } else if (header == kPartiallyReliableChunkDelayMsHeader) {
            auto res = folly::tryTo<uint64_t>(val);
            if (res.hasError()) {
              LOG(ERROR) << __func__ << ": failed to convert " << header << " '"
                         << val << "' to uint64_t";
              txn_->sendAbort();
            }
            chunkDelayMs_ = *res;
          }
        });

    proxygen::HTTPMessage resp;
    resp.setVersionString(getHttpVersion());
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
    resp.setWantsKeepalive(true);
    resp.setPartiallyReliable();
    txn_->sendHeaders(resp);

    if (script.length() == 0) {
      txn_->sendEOM();
    }
  }

  void onUnframedBodyStarted(uint64_t /* offset */) noexcept override {
  }

  void onBody(std::unique_ptr<folly::IOBuf> chain) noexcept override {
    chunkSize_ = chain->computeChainDataLength();
    if (chunkSize_ == 0) {
      txn_->sendEOM();
      return;
    }

    chunkData_ = std::move(chain);

    attachEventBase(folly::EventBaseManager::get()->getEventBase());
    timeoutExpired();
  }

  void timeoutExpired() noexcept override {
    bool eom = (curPartNum_ == script.length() - 1);

    if (script[curPartNum_] == 'b') {
      LOG(INFO) << ": sending body part " << curPartNum_ << " with delay "
                << delayMs_ << " ms";
      txn_->sendBody(chunkData_->clone());
    } else {
      uint64_t nextOffset =
          chunkData_->computeChainDataLength() * (curPartNum_ + 2);
      auto res = txn_->skipBodyTo(nextOffset);
      if (res.hasError()) {
        LOG(ERROR) << __func__ << ": failed to skip body to offset "
                   << nextOffset << ": " << getErrorCodeString(res.error());
        txn_->sendAbort();
        return;
      } else {
        LOG(INFO) << ": skipping body part " << curPartNum_ << " with delay "
                  << delayMs_ << " ms";
      }
    }

    if (eom) {
      LOG(INFO) << __func__ << ":    sending EOM with chunk " << curPartNum_;
      txn_->sendEOM();
    } else {
      delayMs_ = folly::Random::rand64(chunkDelayMs_);
      scheduleTimeout(delayMs_);
    }

    curPartNum_++;
  }

 private:
  uint64_t delayMs_{0};
  uint64_t curPartNum_{0};

  std::unique_ptr<folly::IOBuf> chunkData_{nullptr};
  std::string script;
  uint64_t chunkSize_{0};
  uint64_t chunkDelayMs_{0};
};

/**
 * Sends a body chunk, then waits for a reject from the client.
 * Once reject arrives, sends another body chunk.
 * Number of pieces/skips to send determined by the script received in client
 * headers.
 */
class PrRejectHandler
    : public EchoHandler
    , public folly::AsyncTimeout {
 public:
  explicit PrRejectHandler(const HQParams& params) : EchoHandler(params) {
  }

  PrRejectHandler() = delete;

  void onEOM() noexcept override {
  }

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {

    msg->getHeaders().forEach(
        [&](const std::string& header, const std::string& val) {
          if (header == kPartiallyReliableScriptHeader) {
            script = val;
          } else if (header == kPartiallyReliableChunkSizeHeader) {
            auto res = folly::tryTo<uint64_t>(val);
            if (res.hasError()) {
              LOG(ERROR) << __func__ << ": failed to convert " << header << " '"
                         << val << "' to uint64_t";
              txn_->sendAbort();
              return;
            }
            chunkSize_ = *res;
          } else if (header == kPartiallyReliableChunkDelayCapMsHeader) {
            auto res = folly::tryTo<uint64_t>(val);
            if (res.hasError()) {
              LOG(ERROR) << __func__ << ": failed to convert " << header << " '"
                         << val << "' to uint64_t";
              txn_->sendAbort();
              return;
            }
            clientDelayCapMs_ = *res;
          }
        });

    proxygen::HTTPMessage resp;
    resp.setVersionString(getHttpVersion());
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
    resp.setWantsKeepalive(true);
    resp.setPartiallyReliable();
    txn_->sendHeaders(resp);

    if (chunkSize_ == 0 || script.length() == 0) {
      txn_->sendEOM();
      return;
    }

    attachEventBase(folly::EventBaseManager::get()->getEventBase());
    sendScriptedBody();
  }

  void sendScriptedBody() {
    if (script.length() == 0) {
      txn_->sendEOM();
      return;
    }

    auto curStep = script[0];
    script.erase(0, 1);

    if (curStep == 'b') {
      txn_->sendBody(folly::IOBuf::copyBuffer(std::string(chunkSize_, 'b')));
      if (script.length() == 0) {
        txn_->sendEOM();
      } else {
        scheduleTimeout(clientDelayCapMs_ / 3);
      }
    } else {
      scheduleTimeout(clientDelayCapMs_ * 2);
    }
  }

  void onBodyRejected(uint64_t /* offset */) noexcept override {
    cancelTimeout();
    sendScriptedBody();
  }

  void timeoutExpired() noexcept override {
    sendScriptedBody();
  }

 private:
  std::string script;
  uint64_t chunkSize_{0};
  uint64_t clientDelayCapMs_{0};
};

class ContinueHandler : public EchoHandler {
 public:
  explicit ContinueHandler(const HQParams& params) : EchoHandler(params) {
  }

  ContinueHandler() = delete;

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    VLOG(10) << "ContinueHandler::onHeadersComplete";
    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << getHttpVersion();
    resp.setVersionString(getHttpVersion());
    if (msg->getHeaders().getSingleOrEmpty(proxygen::HTTP_HEADER_EXPECT) ==
        "100-continue") {
      resp.setStatusCode(100);
      resp.setStatusMessage("Continue");
      maybeAddAltSvcHeader(resp);
      txn_->sendHeaders(resp);
    }
    EchoHandler::onHeadersComplete(std::move(msg));
  }
};

class RandBytesGenHandler : public BaseSampleHandler {
 public:
  explicit RandBytesGenHandler(const HQParams& params)
      : BaseSampleHandler(params) {
  }

  RandBytesGenHandler() = delete;

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    auto path = msg->getPathAsStringPiece();
    VLOG(10) << "RandBytesGenHandler::onHeadersComplete";
    VLOG(1) << "Request path: " << path;
    CHECK_GE(path.size(), 1);
    try {
      respBodyLen_ = folly::to<uint64_t>(path.subpiece(1));
    } catch (const folly::ConversionError&) {
      auto errorMsg = folly::to<std::string>(
          "Invalid URL: cannot extract requested response-length from url "
          "path: ",
          path);
      LOG(ERROR) << errorMsg;
      sendError(errorMsg);
      return;
    }
    if (respBodyLen_ > kMaxAllowedLength) {
      sendError(kErrorMsg);
      return;
    }

    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << getHttpVersion();
    resp.setVersionString(getHttpVersion());
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
    maybeAddAltSvcHeader(resp);
    txn_->sendHeaders(resp);
    if (msg->getMethod() == proxygen::HTTPMethod::GET) {
      sendBodyInChunks();
    }
  }

  void onBody(std::unique_ptr<folly::IOBuf> /*chain*/) noexcept override {
    VLOG(10) << "RandBytesGenHandler::onBody";
    sendBodyInChunks();
  }

  void onEOM() noexcept override {
    VLOG(10) << "RandBytesGenHandler::onEOM";
  }

  void onError(const proxygen::HTTPException& /*error*/) noexcept override {
    VLOG(10) << "RandBytesGenHandler::onERROR";
    txn_->sendAbort();
  }

  void onEgressPaused() noexcept override {
    paused_ = true;
  }

  void onEgressResumed() noexcept override {
    paused_ = false;
    sendBodyInChunks();
  }

 private:
  void sendBodyInChunks() {
    if (error_) {
      LOG(ERROR) << "sendBodyInChunks no-op, error_=true";
      txn_->sendAbort();
      return;
    }
    uint64_t iter = respBodyLen_ / kMaxChunkSize;
    if (respBodyLen_ % kMaxChunkSize != 0) {
      ++iter;
    }
    VLOG(10) << "Sending response in " << iter << " chunks";
    for (uint64_t i = 0; i < iter && !paused_; i++) {
      uint64_t chunkSize = std::fmin(kMaxChunkSize, respBodyLen_);
      VLOG(10) << "Sending " << chunkSize << " bytes of data";
      txn_->sendBody(genRandBytes(chunkSize));
      respBodyLen_ -= chunkSize;
    }
    if (!paused_ && !eomSent_ && respBodyLen_ == 0) {
      VLOG(10) << "Sending response EOM";
      txn_->sendEOM();
      eomSent_ = true;
    }
  }

  std::unique_ptr<folly::IOBuf> randBytes(int len) {
    static folly::ThreadLocal<std::vector<uint8_t>> data;
    random_bytes_engine rbe;
    auto previousSize = data->size();
    if (previousSize < size_t(len)) {
      data->resize(len);
      std::generate(begin(*data) + previousSize, end(*data), std::ref(rbe));
    }
    return folly::IOBuf::wrapBuffer(folly::ByteRange(data->data(), len));
  }

  std::unique_ptr<folly::IOBuf> genRandBytes(int len) {
    int contentLength = (len / 2) + 1;
    auto randData = randBytes(contentLength);
    auto hex = folly::hexlify(randData->coalesce());
    hex.resize(len);
    return folly::IOBuf::copyBuffer(hex);
  }

  void sendError(const std::string& errorMsg) {
    proxygen::HTTPMessage resp;
    resp.setStatusCode(400);
    resp.setStatusMessage("Bad Request");
    resp.setWantsKeepalive(true);
    maybeAddAltSvcHeader(resp);
    txn_->sendHeaders(resp);
    txn_->sendBody(folly::IOBuf::copyBuffer(errorMsg));
    txn_->sendEOM();
    error_ = true;
  }

  const uint64_t kMaxAllowedLength{1ULL * 1024 * 1024 * 1024}; // 1 GB
  const uint64_t kMaxChunkSize{100ULL * 1024};             // 100 KB
  const std::string kErrorMsg = folly::to<std::string>(
      "More than 1GB of data requested. ", "Please request for smaller size.");
  uint64_t respBodyLen_;
  bool paused_{false};
  bool eomSent_{false};
  bool error_{false};
};

class DummyHandler : public BaseSampleHandler {
 public:
  explicit DummyHandler(const HQParams& params) : BaseSampleHandler(params) {
  }

  DummyHandler() = delete;

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    VLOG(10) << "DummyHandler::onHeadersComplete";
    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << getHttpVersion();
    resp.setVersionString(getHttpVersion());
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
    resp.setWantsKeepalive(true);
    maybeAddAltSvcHeader(resp);
    txn_->sendHeaders(resp);
    if (msg->getMethod() == proxygen::HTTPMethod::GET) {
      txn_->sendBody(folly::IOBuf::copyBuffer(kDummyMessage));
    }
  }

  void onBody(std::unique_ptr<folly::IOBuf> /*chain*/) noexcept override {
    VLOG(10) << "DummyHandler::onBody";
    txn_->sendBody(folly::IOBuf::copyBuffer(kDummyMessage));
  }

  void onEOM() noexcept override {
    VLOG(10) << "DummyHandler::onEOM";
    txn_->sendEOM();
  }

  void onError(const proxygen::HTTPException& /*error*/) noexcept override {
    txn_->sendAbort();
  }

 private:
  const std::string kDummyMessage =
      folly::to<std::string>("you reached mvfst.net, ",
                             "reach the /echo endpoint for an echo response ",
                             "query /<number> endpoints for a variable size "
                             "response with random bytes");
};

class HealthCheckHandler : public BaseSampleHandler {
 public:
  HealthCheckHandler(bool healthy, const HQParams& params)
      : BaseSampleHandler(params), healthy_(healthy) {
  }

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    VLOG(10) << "HealthCheckHandler::onHeadersComplete";
    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << getHttpVersion();
    resp.setVersionString(getHttpVersion());
    if (msg->getMethod() == proxygen::HTTPMethod::GET) {
      resp.setStatusCode(healthy_ ? 200 : 400);
      resp.setStatusMessage(healthy_ ? "Ok" : "Not Found");
    } else {
      resp.setStatusCode(405);
      resp.setStatusMessage("Method not allowed");
    }
    resp.setWantsKeepalive(true);
    maybeAddAltSvcHeader(resp);
    txn_->sendHeaders(resp);

    txn_->sendBody(
        folly::IOBuf::copyBuffer(healthy_ ? "1-AM-ALIVE" : "1-AM-NOT-WELL"));
  }

  void onBody(std::unique_ptr<folly::IOBuf> /*chain*/) noexcept override {
    VLOG(10) << "HealthCheckHandler::onBody";
    assert(false);
  }

  void onEOM() noexcept override {
    VLOG(10) << "HealthCheckHandler::onEOM";
    txn_->sendEOM();
  }

  void onError(const proxygen::HTTPException& /*error*/) noexcept override {
    txn_->sendAbort();
  }

 private:
  bool healthy_;
};

class WaitReleaseHandler : public BaseSampleHandler {
 public:
  WaitReleaseHandler(folly::EventBase* evb, const HQParams& params)
      : BaseSampleHandler(params), evb_(evb) {
  }

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override;

  void sendErrorResponse(const std::string& body) {
    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << getHttpVersion();
    resp.setVersionString(getHttpVersion());
    resp.setStatusCode(400);
    resp.setStatusMessage("ERROR");
    resp.setWantsKeepalive(false);
    maybeAddAltSvcHeader(resp);
    txn_->sendHeaders(resp);
    txn_->sendBody(folly::IOBuf::copyBuffer(body));
    txn_->sendEOM();
  }

  void sendOkResponse(const std::string& body, bool eom) {
    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << getHttpVersion();
    resp.setVersionString(getHttpVersion());
    resp.setStatusCode(200);
    resp.setStatusMessage("OK");
    resp.setWantsKeepalive(true);
    resp.setIsChunked(true);
    maybeAddAltSvcHeader(resp);
    txn_->sendHeaders(resp);
    txn_->sendBody(folly::IOBuf::copyBuffer(body));
    if (eom) {
      txn_->sendEOM();
    }
  }

  void release() {
    evb_->runImmediatelyOrRunInEventBaseThreadAndWait([this] {
      txn_->sendBody(folly::IOBuf::copyBuffer("released\n"));
      txn_->sendEOM();
    });
  }

  void maybeCleanup();

  void onBody(std::unique_ptr<folly::IOBuf> /*chain*/) noexcept override {
    VLOG(10) << "WaitReleaseHandler::onBody - ignoring";
  }

  void onEOM() noexcept override {
    VLOG(10) << "WaitReleaseHandler::onEOM";
  }

  void onError(const proxygen::HTTPException& /*error*/) noexcept override {
    maybeCleanup();
    txn_->sendAbort();
  }

 private:
  static std::unordered_map<uint, WaitReleaseHandler*>& getWaitingHandlers();

  static std::mutex& getMutex();

  std::string path_;
  uint32_t id_{0};
  folly::EventBase* evb_;
};

namespace {
constexpr auto kPushFileName = "pusheen.txt";
};

class ServerPushHandler : public BaseSampleHandler {
  class ServerPushTxnHandler : public proxygen::HTTPPushTransactionHandler {
    void setTransaction(
        proxygen::HTTPTransaction* /* txn */) noexcept override {
    }

    void detachTransaction() noexcept override {
    }

    void onError(const proxygen::HTTPException& /* err */) noexcept override {
    }

    void onEgressPaused() noexcept override {
    }

    void onEgressResumed() noexcept override {
    }
  };

 public:
  explicit ServerPushHandler(const HQParams& params)
      : BaseSampleHandler(params) {
  }

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> /* msg */) noexcept override;

  void onBody(std::unique_ptr<folly::IOBuf> /* chain */) noexcept override;

  void onEOM() noexcept override;

  void onError(const proxygen::HTTPException& /*error*/) noexcept override;

  void detachTransaction() noexcept override {
  }

 private:
  void sendPushPromise(proxygen::HTTPTransaction* /* pushTxn */,
                       const std::string& /* path */);

  void sendErrorResponse(const std::string& /* body */);

  void sendPushResponse(proxygen::HTTPTransaction* /* pushTxn */,
                        const std::string& /* url */,
                        const std::string& /* body */,
                        bool /* eom */);

  void sendOkResponse(const std::string& /* body */, bool /* eom */);

  std::string path_;
  ServerPushTxnHandler pushTxnHandler_;
};

class StaticFileHandler : public BaseSampleHandler {
 public:
  explicit StaticFileHandler(const HQParams& params)
      : BaseSampleHandler(params), staticRoot_(params.staticRoot) {
  }

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    auto path = msg->getPathAsStringPiece();
    VLOG(10) << "StaticFileHandler::onHeadersComplete";
    VLOG(4) << "Request path: " << path;
    if (path.contains("..")) {
      sendError("Path cannot contain ..");
      return;
    }
    try {
      // Strip /static/ and join with /.
      file_ = std::make_unique<folly::File>(
          folly::to<std::string>(staticRoot_, "/", path));
    } catch (const std::system_error&) {
      auto errorMsg = folly::to<std::string>(
          "Invalid URL: cannot open requested file. "
          "path: ",
          path);
      LOG(ERROR) << errorMsg;
      sendError(errorMsg);
      return;
    }
    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << getHttpVersion();
    resp.setVersionString(getHttpVersion());
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
    maybeAddAltSvcHeader(resp);
    txn_->sendHeaders(resp);
    // use a CPU executor since read(2) of a file can block
    folly::getCPUExecutor()->add(
        std::bind(&StaticFileHandler::readFile,
                  this,
                  folly::EventBaseManager::get()->getEventBase()));
  }

  void onBody(std::unique_ptr<folly::IOBuf> /*chain*/) noexcept override {
  }

  void onEOM() noexcept override {
  }

  void onError(const proxygen::HTTPException& /*error*/) noexcept override {
    VLOG(10) << "StaticFileHandler::onError";
    txn_->sendAbort();
  }

  void onEgressPaused() noexcept override {
    VLOG(10) << "StaticFileHandler::onEgressPaused";
    paused_ = true;
  }

  void onEgressResumed() noexcept override {
    VLOG(10) << "StaticFileHandler::onEgressResumed";
    paused_ = false;
    folly::getCPUExecutor()->add(
        std::bind(&StaticFileHandler::readFile,
                  this,
                  folly::EventBaseManager::get()->getEventBase()));
  }

 private:
  void readFile(folly::EventBase* evb) {
    folly::IOBufQueue buf;
    while (file_ && !paused_) {
      // read 4k-ish chunks and foward each one to the client
      auto data = buf.preallocate(4096, 4096);
      auto rc = folly::readNoInt(file_->fd(), data.first, data.second);
      if (rc < 0) {
        // error
        VLOG(4) << "Read error=" << rc;
        file_.reset();
        evb->runInEventBaseThread([this] {
          LOG(ERROR) << "Error reading file";
          txn_->sendAbort();
        });
        break;
      } else if (rc == 0) {
        // done
        file_.reset();
        VLOG(4) << "Read EOF";
        evb->runInEventBaseThread([this] { txn_->sendEOM(); });
        break;
      } else {
        buf.postallocate(rc);
        evb->runInEventBaseThread([this, body = buf.move()]() mutable {
          txn_->sendBody(std::move(body));
        });
      }
    }
  }

  void sendError(const std::string& errorMsg) {
    proxygen::HTTPMessage resp;
    resp.setStatusCode(400);
    resp.setStatusMessage("Bad Request");
    resp.setWantsKeepalive(true);
    maybeAddAltSvcHeader(resp);
    txn_->sendHeaders(resp);
    txn_->sendBody(folly::IOBuf::copyBuffer(errorMsg));
    txn_->sendEOM();
  }

  std::unique_ptr<folly::File> file_;
  std::atomic<bool> paused_{false};
  const std::string staticRoot_;
};

}} // namespace quic::samples
