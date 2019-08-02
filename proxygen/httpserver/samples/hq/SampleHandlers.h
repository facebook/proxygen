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

#include <algorithm>
#include <climits>
#include <functional>
#include <math.h>
#include <mutex>
#include <random>
#include <vector>

//#include <common/logging/logging.h>
#include <folly/FileUtil.h>
#include <folly/Random.h>
#include <folly/ThreadLocal.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBaseManager.h>
#include <proxygen/httpserver/samples/hq/PartiallyReliableCurlClient.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>

namespace quic { namespace samples {

using random_bytes_engine =
    std::independent_bits_engine<std::default_random_engine,
                                 CHAR_BIT,
                                 unsigned char>;

class BaseQuicHandler : public proxygen::HTTPTransactionHandler {
 public:
  explicit BaseQuicHandler(const std::string& version) : version_(version) {
  }

  BaseQuicHandler() : version_("1.1") {
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
  proxygen::HTTPTransaction* txn_{nullptr};
  std::string version_;
};

class EchoHandler : public BaseQuicHandler {
 public:
  explicit EchoHandler(const std::string& version) : BaseQuicHandler(version) {
  }

  EchoHandler() = default;

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    VLOG(10) << "EchoHandler::onHeadersComplete";
    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << version_;
    sendFooter_ =
        (msg->getHTTPVersion() == proxygen::HTTPMessage::kHTTPVersion09);
    resp.setVersionString(version_);
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
    msg->getHeaders().forEach(
        [&](const std::string& header, const std::string& val) {
          resp.getHeaders().add(folly::to<std::string>("x-echo-", header), val);
        });
    resp.stripPerHopHeaders();
    resp.setWantsKeepalive(true);
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

/**
 * Streams ASCII cat in partially realible mode.
 */
class PrCatHandler
    : public EchoHandler
    , public proxygen::HTTPTransactionTransportCallback
    , public folly::AsyncTimeout {
 public:
  explicit PrCatHandler(const std::string& version,
                            folly::Optional<uint64_t> prChunkSize,
                            folly::Optional<uint64_t> prChunkDelayMs)
      : EchoHandler(version),
        chunkSize_(prChunkSize),
        chunkDelayMs_(prChunkDelayMs) {
    if (!chunkSize_) {
      chunkSize_ = 16;
    }
    if (!chunkDelayMs_) {
      chunkDelayMs_ = 0;
    }
  }

  PrCatHandler() = delete;

  void setPrParams(folly::Optional<uint64_t> prChunkSize,
                   folly::Optional<uint64_t> prChunkDelayMs) {
    chunkSize_ = prChunkSize;
    chunkDelayMs_ = prChunkDelayMs;
  }

  // HTTPTransactionTransportCallback
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
    VLOG(10) << "Setting http-version to " << version_;
    resp.setVersionString(version_);
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
    resp.getHeaders().add("pr-chunk-size", folly::to<std::string>(*chunkSize_));
    resp.stripPerHopHeaders();
    resp.setWantsKeepalive(true);
    resp.setPartiallyReliable();
    txn_->setTransportCallback(this);
    txn_->sendHeaders(resp);
  }

  void onBodyPeek(uint64_t offset,
                       const folly::IOBufQueue& chain) noexcept override {
    LOG(INFO) << __func__ << ": got " << chain.chainLength()
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
  folly::Optional<uint64_t> chunkSize_;
  folly::Optional<uint64_t> chunkDelayMs_;
  PartiallyReliableSender dataSndr_{*chunkSize_};
};

class ContinueHandler : public EchoHandler {
 public:
  explicit ContinueHandler(const std::string& version) : EchoHandler(version) {
  }

  ContinueHandler() = default;

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    VLOG(10) << "ContinueHandler::onHeadersComplete";
    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << version_;
    resp.setVersionString(version_);
    if (msg->getHeaders().getSingleOrEmpty(proxygen::HTTP_HEADER_EXPECT) ==
        "100-continue") {
      resp.setStatusCode(100);
      resp.setStatusMessage("Continue");
      txn_->sendHeaders(resp);
    }
    EchoHandler::onHeadersComplete(std::move(msg));
  }
};

class RandBytesGenHandler : public BaseQuicHandler {
 public:
  explicit RandBytesGenHandler(const std::string& version)
      : BaseQuicHandler(version) {
  }

  RandBytesGenHandler() = default;

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    VLOG(10) << "RandBytesGenHandler::onHeadersComplete";
    VLOG(1) << "Request path: " << msg->getPath();
    CHECK(msg->getPath().size() > 1);
    try {
      respBodyLen_ = folly::to<uint64_t>(msg->getPath().substr(1));
    } catch (const folly::ConversionError& ex) {
      auto errorMsg = folly::to<std::string>(
          "Invalid URL: cannot extract requested response-length from url "
          "path: ",
          msg->getPath());
      LOG(ERROR) << errorMsg;
      sendError(errorMsg);
      return;
    }
    if (respBodyLen_ > kMaxAllowedLength) {
      sendError(kErrorMsg);
      return;
    }

    proxygen::HTTPMessage resp;
    VLOG(10) << "Setting http-version to " << version_;
    resp.setVersionString(version_);
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
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
    resp.stripPerHopHeaders();
    resp.setWantsKeepalive(true);
    txn_->sendHeaders(resp);
    txn_->sendBody(folly::IOBuf::copyBuffer(errorMsg));
  }

  const uint64_t kMaxAllowedLength{10 * 1024 * 1024}; // 10 MB
  const uint64_t kMaxChunkSize{100 * 1024};           // 100 KB
  const std::string kErrorMsg =
      folly::to<std::string>("More than 10 MB of data requested. ",
                             "Please request for smaller size.");
  uint64_t respBodyLen_;
  bool paused_{false};
  bool eomSent_{false};
};

class DummyHandler : public BaseQuicHandler {
 public:
  explicit DummyHandler(const std::string& version) : BaseQuicHandler(version) {
  }

  DummyHandler() = default;

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    VLOG(10) << "DummyHandler::onHeadersComplete";
    proxygen::HTTPMessage resp;
    resp.setVersionString(version_);
    resp.setStatusCode(200);
    resp.setStatusMessage("Ok");
    resp.stripPerHopHeaders();
    resp.setWantsKeepalive(true);
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

class HealthCheckHandler : public BaseQuicHandler {
 public:
  HealthCheckHandler(bool healthy, const std::string& version)
      : BaseQuicHandler(version), healthy_(healthy) {
  }

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override {
    VLOG(10) << "HealthCheckHandler::onHeadersComplete";
    proxygen::HTTPMessage resp;
    resp.setVersionString(version_);
    if (msg->getMethod() == proxygen::HTTPMethod::GET) {
      resp.setStatusCode(healthy_ ? 200 : 400);
      resp.setStatusMessage(healthy_ ? "Ok" : "Not Found");
    } else {
      resp.setStatusCode(405);
      resp.setStatusMessage("Method not allowed");
    }
    resp.stripPerHopHeaders();
    resp.setWantsKeepalive(true);
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

class WaitReleaseHandler : public BaseQuicHandler {
 public:
  WaitReleaseHandler(folly::EventBase* evb, const std::string& version)
      : BaseQuicHandler(version), evb_(evb) {
  }

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override;

  void sendErrorResponse(const std::string& body) {
    proxygen::HTTPMessage resp;
    resp.setVersionString(version_);
    resp.setStatusCode(400);
    resp.setStatusMessage("ERROR");
    resp.setWantsKeepalive(false);
    txn_->sendHeaders(resp);
    txn_->sendBody(folly::IOBuf::copyBuffer(body));
    txn_->sendEOM();
  }

  void sendOkResponse(const std::string& body, bool eom) {
    proxygen::HTTPMessage resp;
    resp.setVersionString(version_);
    resp.setStatusCode(200);
    resp.setStatusMessage("OK");
    resp.setWantsKeepalive(true);
    resp.setIsChunked(true);
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

class ServerPushHandler : public BaseQuicHandler {
  class ServerPushTxnHandler : public proxygen::HTTPPushTransactionHandler {
    void setTransaction(
        proxygen::HTTPTransaction* /* txn */) noexcept override {
    }

    void detachTransaction() noexcept override {
    }

    void onError(const proxygen::HTTPException& /* error */) noexcept override {
    }

    void onEgressPaused() noexcept override {
    }

    void onEgressResumed() noexcept override {
    }
  };

 public:
  explicit ServerPushHandler(const std::string& version);

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

}} // namespace quic::samples
