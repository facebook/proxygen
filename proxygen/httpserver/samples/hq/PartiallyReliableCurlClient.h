/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Optional.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>

#include <map>

#include <proxygen/httpclient/samples/curl/CurlClient.h>

namespace quic { namespace samples {

class PartiallyReliablePeerBase;
using PartiallyReliableReceiver = PartiallyReliablePeerBase;

/**
 * Example class that implements a partially realible HTTP client.
 * Can connect to HTTP server implementing partial reliability and enforce
 * user-specified latency cap on receiving body chunks.
 */
class PartiallyReliableCurlClient
    : public CurlService::CurlClient
    , public folly::AsyncTimeout {

  using Clock = std::chrono::high_resolution_clock;
  using Ms = std::chrono::milliseconds;
  using Sec = std::chrono::seconds;

 public:
  PartiallyReliableCurlClient(
      folly::EventBase* evb,
      proxygen::HTTPMethod httpMethod,
      const proxygen::URL& url,
      const proxygen::URL* proxy,
      const proxygen::HTTPHeaders& headers,
      const std::string& inputFilename,
      bool h2c = false,
      unsigned short httpMajor = 1,
      unsigned short httpMinor = 1,
      folly::Optional<uint64_t> prChunkDelayMs = folly::none)
      : CurlService::CurlClient(evb,
                                httpMethod,
                                url,
                                proxy,
                                headers,
                                inputFilename,
                                h2c,
                                httpMajor,
                                httpMinor,
                                true /* partiallyRealible */),
        delayCapMs_(prChunkDelayMs) {
    evb_ = evb;
    if (delayCapMs_ && (*delayCapMs_ == 0)) {
      delayCapMs_ = folly::none;
    }
  }

  void onHeadersComplete(
      std::unique_ptr<proxygen::HTTPMessage> msg) noexcept override;

  void onEOM() noexcept override;

  void onError(const proxygen::HTTPException& error) noexcept override;

  void onBodyPeek(uint64_t offset,
                  const folly::IOBuf& /* chain */) noexcept override;

  void onBodySkipped(uint64_t offset) noexcept override;

  void onBodyRejected(uint64_t offset) noexcept override;

  void onBodyWithOffset(uint64_t bodyOffset,
                        std::unique_ptr<folly::IOBuf> chain) noexcept override;

  void timeoutExpired() noexcept override;

 private:
  folly::EventBase* evb_;
  std::chrono::time_point<Clock> prevTs_;
  uint64_t bodyPartNum_{0};
  folly::Optional<uint64_t> delayCapMs_;
  folly::Optional<uint64_t> chunkSize_;
  std::unique_ptr<PartiallyReliableReceiver> dataRcvd_{nullptr};
};

/**
 * Helper class to run accounting for latency of received data and number of
 * chunks received/expected.
 */
class PartiallyReliablePeerBase {
 public:
  explicit PartiallyReliablePeerBase(uint64_t chunkSize)
      : chunkSize_(chunkSize) {
  }

  void recordChunk(uint64_t chunkNum,
                   uint64_t delayMs,
                   std::unique_ptr<folly::IOBuf> data);

  void printData();

 protected:
  uint64_t chunkSize_;

  struct Chunk {
    uint64_t delayMs;
    std::unique_ptr<folly::IOBuf> data;
  };
  std::list<Chunk> chunkList_;
};

class PartiallyReliableSender : public PartiallyReliablePeerBase {
 public:
  explicit PartiallyReliableSender(uint64_t chunkSize)
      : PartiallyReliablePeerBase(chunkSize) {
    auto buf = folly::IOBuf::copyBuffer(cat);
    sndBuf_.append(std::move(buf));
  }

  folly::Optional<std::unique_ptr<folly::IOBuf>> generateChunk();

  bool hasMoreData() const;

 private:
  folly::IOBufQueue sndBuf_{folly::IOBufQueue::cacheChainLength()};
  // clang-format off
   const std::string cat{
 "                                                                               \n"
 "                     .............                .***.             .***.      \n"
 "             ...*****             *****...       $   . *.         .* .   $     \n"
 "         ..**        .   .   .   .   .    ..    $   $$$. *. ... .* .$$$   $    \n"
 "       .*    . * . * . * . * . * . * . * .  ** .*  $$$***  *   *  ***$$$  *.   \n"
 "     .*      . * . * . * . * . * . * . * .     $  *                    *   $   \n"
 "    .*   . * . * . *           *   * . * . *  .*      ...          ...     *.  \n"
 "   .*    . * . *    ..*********...     * . *  $     .$*              *$.    $  \n"
 "  .*     . * . * .**     .   .    **..   . * $ *.      .**$     .**$      .* $ \n"
 " .*    * . * .       . * . * . * .    $    * $ *      *  $$    *  $$       * $ \n"
 " $     * . * . * . * . * . * . * . *   $     $             $$.$$             $ \n"
 " $     * . * . * . * . * . * . * . * .  $  * $  * .        $$$$$        . *  $ \n"
 " $     * . * . * . * . * . * . * . * .  $    $      *  ..   *$*   ..  *      $ \n"
 " *.    * . * . * . * . * . * . * . *   .*  *  $  . . . $  . .*. .  $ . . .  $  \n"
 "  $    * . * . * . * . * . * . * . *  .*   *            *..*   *..*            \n"
 "   $     . * . * . * . * . * . *   ..*   . * . *..    *             *    ..*   \n"
 "   *.      * . * . * . * . * .  .**    * . * .    ***$...         ...$***      \n"
 "    *. *..     * . * . * . * .  *........  *    .....  .***.....***            \n"
 "      *. .*$*.....                       $...*$*$*.*   $*.$*... `*:....        \n"
 "        **..*    $*$*$*$***$........$*$*$*  .*.*.*  ...**      .**.    `**.    \n"
 "            ***.$.$.* .*  .*.*.*    .*.*.* $.$.$****.......  *. *. $ *. *. $   \n"
 "                   ***.$.$.$.$.....$.$.****               **..$..$.*..$..$.*   \n"
 "                                                                               \n"
      // clang-format on
  };
};

}} // namespace quic::samples
