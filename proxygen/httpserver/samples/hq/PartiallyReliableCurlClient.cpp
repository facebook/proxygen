/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <proxygen/httpserver/samples/hq/PartiallyReliableCurlClient.h>

namespace quic { namespace samples {
void PartiallyReliableCurlClient::onHeadersComplete(
    std::unique_ptr<proxygen::HTTPMessage> msg) noexcept {
  const std::string& value =
      msg->getHeaders().getSingleOrEmpty("pr-chunk-size");
  if (value.length() > 0) {
    chunkSize_ = folly::to<uint64_t>(value);
  }

  if (!chunkSize_.has_value()) {
    LOG(ERROR) << __func__
               << ": no pr-chunk-size header found in response headers";
    txn_->sendEOM();
    return;
  }

  LOG_IF(INFO, loggingEnabled_)
      << __func__ << ": chunkSize_ is set to " << *chunkSize_;

  dataRcvd_ = std::make_unique<PartiallyReliableReceiver>(*chunkSize_);

  attachEventBase(evb_);
  if (delayCapMs_) {
    scheduleTimeout(*delayCapMs_);
  }
  prevTs_ = Clock::now();
}

void PartiallyReliableCurlClient::onEOM() noexcept {
  LOG_IF(INFO, loggingEnabled_) << "Got EOM";
  cancelTimeout();
  dataRcvd_->printData();
}

void PartiallyReliableCurlClient::onError(
    const proxygen::HTTPException& error) noexcept {
  LOG(ERROR) << ": transaction error: " << error;
  cancelTimeout();
}

void PartiallyReliableCurlClient::onBodyPeek(
    uint64_t offset, const folly::IOBuf& /* chain */) noexcept {
  LOG_IF(INFO, loggingEnabled_)
      << "Got " << __func__ << " at offset " << offset;
}

void PartiallyReliableCurlClient::onBodySkipped(uint64_t offset) noexcept {
  LOG_IF(INFO, loggingEnabled_)
      << "Got " << __func__ << " at offset " << offset;
}

void PartiallyReliableCurlClient::onBodyRejected(uint64_t offset) noexcept {
  LOG_IF(INFO, loggingEnabled_)
      << "Got " << __func__ << " at offset " << offset;
}

void PartiallyReliableCurlClient::onBodyWithOffset(
    uint64_t bodyOffset, std::unique_ptr<folly::IOBuf> chain) noexcept {
  // Cancel any pending timeouts.
  cancelTimeout();

  auto curTs = Clock::now();
  auto tsDiff = curTs - prevTs_;
  prevTs_ = curTs;

  LOG_IF(INFO, loggingEnabled_)
      << __func__ << ": got body part " << bodyPartNum_ << " at offset "
      << bodyOffset << " (delay = " << tsDiff.count() / 1000000 << " ms):";
  dataRcvd_->recordChunk(
      bodyPartNum_, tsDiff.count() / 1000000, std::move(chain));
  bodyPartNum_++;

  // Schedule a timeout for the next chunk (latency cap).
  if (delayCapMs_) {
    scheduleTimeout(*delayCapMs_);
  }
}

void PartiallyReliableCurlClient::timeoutExpired() noexcept {
  LOG(ERROR) << __func__ << ": cancelling data for chunk " << bodyPartNum_;
  dataRcvd_->recordChunk(bodyPartNum_, *delayCapMs_, nullptr);

  auto res = txn_->rejectBodyTo((bodyPartNum_ + 1) * chunkSize_.value());
  if (res.hasError()) {
    LOG(ERROR) << __func__ << ": error: "; // << res.error();
  }
  bodyPartNum_++;

  if (delayCapMs_) {
    scheduleTimeout(*delayCapMs_);
  }

  prevTs_ = Clock::now();
}

void PartiallyReliablePeerBase::recordChunk(
    uint64_t /* chunkNum */,
    uint64_t delayMs,
    std::unique_ptr<folly::IOBuf> data) {
  chunkList_.push_back(Chunk{delayMs, std::move(data)});
}

void PartiallyReliablePeerBase::printData() {
  const uint64_t lineWidth = 80;
  uint64_t totalDelayMs = 0;

  std::cout.flush();

  LOG(INFO) << ": delay histogram:";
  int i = 0;
  for (const auto& chunk : chunkList_) {

    LOG(INFO) << "Chunk " << i << ":   " << chunk.delayMs << " ms"
              << (chunk.data ? "" : " (skipped)");
    i++;
  }

  std::cout.flush();
  uint64_t printedChars = 0;
  for (const auto& chunk : chunkList_) {
    if (chunk.data) {
      // Data.
      std::cout.write((const char*)chunk.data->data(),
                      chunk.data->computeChainDataLength());
      printedChars += chunk.data->computeChainDataLength();
    } else {
      // Skip.
      uint64_t j = 0;
      while (j < chunkSize_) {
        std::cout.write("&", 1);
        j++;
        printedChars++;
        if ((printedChars != 0) && (printedChars % lineWidth == 0)) {
          std::cout << std::endl;
        }
      }
    }
    totalDelayMs += chunk.delayMs;
  }
  LOG(INFO) << ": got the whole cat in " << totalDelayMs << " ms";
  std::cout.flush();
}

folly::Optional<std::unique_ptr<folly::IOBuf>>
PartiallyReliableSender::generateChunk() {
  if (sndBuf_.chainLength() != 0) {
    return sndBuf_.splitAtMost(chunkSize_);
  } else {
    return folly::none;
  }
}

bool PartiallyReliableSender::hasMoreData() const {
  return !sndBuf_.empty();
}

}} // namespace quic::samples
