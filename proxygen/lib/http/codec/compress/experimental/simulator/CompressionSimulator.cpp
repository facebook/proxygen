/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "proxygen/lib/http/codec/compress/experimental/simulator/CompressionSimulator.h"
#include "proxygen/lib/http/codec/compress/experimental/simulator/QCRAMScheme.h"
#include "proxygen/lib/http/codec/compress/experimental/simulator/QPACKScheme.h"
#include "proxygen/lib/http/codec/compress/experimental/simulator/QMINScheme.h"
#include "proxygen/lib/http/codec/compress/experimental/simulator/HPACKScheme.h"
#include <folly/MoveWrapper.h>
#include <proxygen/lib/http/codec/compress/test/HTTPArchive.h>
#include <proxygen/lib/utils/TestUtils.h>
#include <proxygen/lib/utils/Time.h>

using namespace std;
using namespace folly;
using namespace proxygen;

namespace {
using namespace proxygen::compress;

// This needs to be synchronized with HPACKEncoder::kAutoFlushThreshold.
const size_t kMTU = 1400;

const std::string kTestDir = getContainingDirectory(__FILE__).str();

std::string combineCookieCrumbsSorted(std::vector<std::string> crumbs) {
  std::string retval;
  sort(crumbs.begin(), crumbs.end());
  folly::join("; ", crumbs.begin(), crumbs.end(), retval);
  return retval;
}

bool containsAllHeaders(const HTTPHeaders& h1, const HTTPHeaders& h2) {
  bool allValuesPresent = true;
  bool verifyCookies = false;
  h1.forEachWithCode(
    [&] (HTTPHeaderCode code, const string& name, const string& value1) {
      bool h2HasValue = h2.forEachValueOfHeader(
        code, [&value1] (const std::string& value2) {
          return (value1 == value2);
        });
      if (!h2HasValue && code == HTTP_HEADER_COOKIE) {
        verifyCookies = true;
        return;
      }
      DCHECK(h2HasValue) << "h2 does not contain name=" << name << " value="
                         << value1;
      allValuesPresent &= h2HasValue;
    });

  if (verifyCookies) {
    const HTTPHeaders* headers[] = { &h1, &h2, };
    std::string cookies[2] = { "", "", };
    unsigned i;
    for (i = 0; i < 2; ++i) {
      std::vector<std::string> crumbs;
      headers[i]->forEachValueOfHeader(
        HTTP_HEADER_COOKIE,
        [&] (const std::string& crumb) {
          crumbs.push_back(crumb);
          return false;
        });
      cookies[i] = combineCookieCrumbsSorted(crumbs);
    }
    if (cookies[0] == cookies[1]) {
      LOG(INFO) << "Cookie crumbs are reordered";
    } else {
      LOG(INFO) << "Cookies are not equal: `" << cookies[0] << "' vs. `"
                << cookies[1] << "'";
      return false;
    }
  }

  return allValuesPresent;
}

void verifyHeaders(const HTTPMessage& msg1,
                   const HTTPMessage& msg2) {
  DCHECK_EQ(msg1.getMethodString(), msg2.getMethodString());
  DCHECK_EQ(msg1.getURL(), msg2.getURL());
  DCHECK_EQ(msg1.isSecure(), msg2.isSecure());
  DCHECK(containsAllHeaders(msg1.getHeaders(), msg2.getHeaders()));
  DCHECK(containsAllHeaders(msg2.getHeaders(), msg1.getHeaders()));
}

}

namespace proxygen { namespace compress {

bool
CompressionSimulator::readInputFromFileAndSchedule(const string& filename) {
  unique_ptr<HTTPArchive> har;
  try {
    har = HTTPArchive::fromFile(kTestDir + filename);
  } catch (const std::exception& ex) {
    LOG(ERROR) << folly::exceptionStr(ex);
  }
  if (!har || har->requests.size() == 0) {
    return false;
  }
  // Sort by start time (har ordered by finish time?)
  std::sort(har->requests.begin(), har->requests.end(),
            [] (const HTTPMessage& a, const HTTPMessage& b) {
              return a.getStartTime() < b.getStartTime();
            });
  TimePoint last = har->requests[0].getStartTime();
  std::chrono::milliseconds cumulativeDelay(0);
  uint16_t index = 0;
  for (HTTPMessage& msg: har->requests) {
    auto delayFromPrevious = millisecondsBetween(msg.getStartTime(), last);
    // If there was a quiescent gap in the HAR of at least some value, shrink
    // it so the test doesn't last forever
    if (delayFromPrevious > std::chrono::milliseconds(1000)) {
      delayFromPrevious = std::chrono::milliseconds(1000);
    }
    last = msg.getStartTime();
    cumulativeDelay += delayFromPrevious;
    setupRequest(index++, std::move(msg), cumulativeDelay);
  }
  return true;
}

void CompressionSimulator::run() {
#ifndef HAVE_REAL_QMIN
  if (params_.type == SchemeType::QMIN) {
    LOG(INFO) << "QMIN not available";
    return;
  }
#endif

  LOG(INFO) << "Starting run";
  eventBase_.loop();
  uint32_t holBlockCount = 0;
  for (auto& scheme: domains_) {
    holBlockCount += scheme.second->getHolBlockCount();
  }
  LOG(INFO) << "Complete" <<
    "\nStats:"
    "\nSeed: " << params_.seed <<
    "\nBlocks sent: " << requests_.size() <<
    "\nAllowed OOO: " << stats_.allowedOOO <<
    "\nPacket Losses: " << stats_.packetLosses <<
    "\nHOL Block Count: " << holBlockCount <<
    "\nHOL Delay (ms): " << stats_.holDelay.count() <<
    "\nMax Queue Buffer Bytes: " << stats_.maxQueueBufferBytes <<
    "\nUncompressed Bytes: " << stats_.uncompressed <<
    "\nCompressed Bytes: " << stats_.compressed <<
    "\nCompression Ratio: " <<
    int(100 - double(100 * stats_.compressed) / stats_.uncompressed);
}

void CompressionSimulator::setupRequest(uint16_t index, HTTPMessage&& msg,
                                        std::chrono::milliseconds encodeDelay) {
  auto scheme = getScheme(
    msg.getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST));
  requests_.emplace_back(msg);
  auto decodeCompleteCB =
    [index, this, scheme] (std::chrono::milliseconds holDelay) {
    // record processed timestamp
    CHECK(callbacks_[index].getResult().isOk());
    verifyHeaders(requests_[index], *callbacks_[index].getResult().ok());
    stats_.holDelay += holDelay;
    VLOG(1) << "Finished decoding request=" << index << " with holDelay=" <<
    holDelay.count() << " cumulative HoL delay=" << stats_.holDelay.count();
    sendAck(scheme, scheme->getAck(callbacks_[index].seqn));
  };
  callbacks_.emplace_back(index, decodeCompleteCB);

  // Schedule the encode event
  scheduleEvent([index, this, scheme] {
      auto schemeIndex = scheme->index;
      auto encodeRes = encode(scheme, index);
      bool allowOOO = encodeRes.first;
      if (schemeIndex < minOOOThresh()) {
        allowOOO = false;
        auto ack = scheme->getAck(schemeIndex);
        if (ack) {
          scheme->recvAck(std::move(ack));
        }
      }
      stats_.allowedOOO += (allowOOO) ? 1 : 0;
      scheme->encodedBlocks.emplace_back(
        allowOOO, std::move(encodeRes.second), &callbacks_[index]);
      eventBase_.runInLoop(scheme, true);
    }, encodeDelay);
}

// Once per loop, each connection flushes it's encode blocks and schedules
// decodes based on how many packets the block occupies
void CompressionScheme::runLoopCallback() noexcept {
  simulator_->flushSchemePackets(this);
}

void CompressionSimulator::flushSchemePackets(CompressionScheme* scheme) {
  CHECK(!scheme->encodedBlocks.empty());
  VLOG(2) << "Flushing " << scheme->encodedBlocks.size() << " requests";
  // tracks the number of bytes in the current simulated packet
  size_t packetBytes = 0;
  auto encodeRes = &scheme->encodedBlocks.front();
  size_t headerBlockBytesRemaining =
    std::get<1>(*encodeRes)->computeChainDataLength();
  std::chrono::milliseconds packetDelay = deliveryDelay();
  std::chrono::milliseconds decodeDelay = packetDelay;
  while (true) {
    // precondition packetBytes < kMTU
    bool nextPacket = false;
    if (packetBytes + headerBlockBytesRemaining >= kMTU) {
      // Header block filled current packet, triggering a flush
      VLOG(2) << "Request(s) spanned multiple packets";
      nextPacket = true;
    } else {
      packetBytes += headerBlockBytesRemaining;
    }
    headerBlockBytesRemaining -= std::min(headerBlockBytesRemaining,
                                          kMTU - packetBytes);
    if (headerBlockBytesRemaining == 0) {
      // The entire request has been packetized, schedule the decode
      scheduleEvent({
          [this, allowOOO=std::get<0>(*encodeRes),
           buf=std::move(std::get<1>(*encodeRes)),
           cb=std::get<2>(*encodeRes), scheme] () mutable {
            decode(scheme, allowOOO, std::move(buf), *cb);
          }}, decodeDelay);
      scheme->encodedBlocks.pop_front();
      if (scheme->encodedBlocks.empty()) {
        // All done
        break;
      }
      // Grab the next request
      encodeRes = &scheme->encodedBlocks.front();
      headerBlockBytesRemaining =
        std::get<1>(*encodeRes)->computeChainDataLength();
      // This new request will either fit entirely in the current packet, or
      // be in a new packet.  Set its decodeDelay accordingly.
      decodeDelay = nextPacket ? std::chrono::milliseconds(0) : packetDelay;
    }
    if (nextPacket) {
      packetBytes = 0;
      packetDelay = deliveryDelay();
      decodeDelay = std::max(decodeDelay, packetDelay);
    }
  }
  CHECK(scheme->encodedBlocks.empty());
}

CompressionScheme* CompressionSimulator::getScheme(StringPiece domain) {
  static string blended("\"Facebook\"");
  if (params_.blend &&
      (domain.endsWith(".facebook.com") || domain.endsWith(".fbcdn.net"))) {
    domain = blended;
  }

  auto it = domains_.find(domain.str());
  CompressionScheme* scheme = nullptr;
  if (it == domains_.end()) {
    LOG(INFO) << "Creating scheme for domain=" << domain;
    auto schemePtr = makeScheme();
    scheme = schemePtr.get();
    domains_.emplace(domain.str(), std::move(schemePtr));
  } else {
    scheme = it->second.get();
  }
  return scheme;
}

unique_ptr<CompressionScheme> CompressionSimulator::makeScheme() {
  if (params_.type == SchemeType::QCRAM) {
    return make_unique<QCRAMScheme>(this);
  } else if (params_.type == SchemeType::QPACK) {
    return make_unique<QPACKScheme>(this);
  } else if (params_.type == SchemeType::QMIN) {
    return make_unique<QMINScheme>(this);
  } else if (params_.type == SchemeType::HPACK) {
    return make_unique<HPACKScheme>(this, params_.tableSize);
  }
  LOG(FATAL) << "Bad scheme";
  return nullptr;
}

std::pair<bool, unique_ptr<IOBuf>>
CompressionSimulator::encode(CompressionScheme* scheme, uint16_t index) {
  vector<compress::Header> allHeaders;
  // The encode API is pretty bad.  We should just let HPACK directly encode
  // HTTP messages
  HTTPMessage& msg = requests_[index];
  const string& method = msg.getMethodString();
  allHeaders.emplace_back(HTTP_HEADER_COLON_METHOD, http2::kMethod, method);
  if (msg.getMethod() != HTTPMethod::CONNECT) {
    const string& scheme = (msg.isSecure() ? http2::kHttps : http2::kHttp);
    const string& path = msg.getURL();
    allHeaders.emplace_back(HTTP_HEADER_COLON_SCHEME, http2::kScheme, scheme);
    allHeaders.emplace_back(HTTP_HEADER_COLON_PATH, http2::kPath, path);
  }
  msg.getHeaders().removeByPredicate(
    [&] (HTTPHeaderCode, const string& name, const string&) {
      // HAR files contain actual serialized headers protocol headers like
      // :authority, which we are re-adding above.  Strip them so our
      // equality test works
      return (name.size() > 0 && name[0] == ':');
    });

  const HTTPHeaders& headers = msg.getHeaders();
  const string& host = headers.getSingleOrEmpty(HTTP_HEADER_HOST);
  if (!host.empty()) {
    allHeaders.emplace_back(
      HTTP_HEADER_COLON_AUTHORITY, http2::kAuthority, host);
  }
  // Cookies are coalesced in the HAR file but need to be added as separate
  // headers to optimize compression ratio
  vector<string> cookies;
  headers.forEachWithCode(
    [&] (HTTPHeaderCode code, const string& name, const string& value) {
      if (code == HTTP_HEADER_COOKIE) {
        vector<StringPiece> cookiePieces;
        folly::split(';', value, cookiePieces);
        cookies.reserve(cookies.size() + cookiePieces.size());
        for (auto cookie: cookiePieces) {
          cookies.push_back(ltrimWhitespace(cookie).str());
          allHeaders.emplace_back(code, name, cookies.back());
        }
      } else if (code != HTTP_HEADER_HOST) {
        allHeaders.emplace_back(code, name, value);
      }
    });
  auto res = scheme->encode(std::move(allHeaders), stats_);
  VLOG(1) << "Encoded request=" << index << " for host=" << host
          << " block size=" << res.second->computeChainDataLength()
          << " cumulative compression ratio=" <<
    int(100 - double(100 * stats_.compressed) / stats_.uncompressed);
  return res;
}

void CompressionSimulator::decode(CompressionScheme* scheme, bool allowOOO,
                                  unique_ptr<IOBuf> encodedReq,
                                  SimStreamingCallback& cb) {
  scheme->decode(allowOOO, std::move(encodedReq), stats_, cb);
}

void CompressionSimulator::scheduleEvent(folly::Function<void()> f,
                                         std::chrono::milliseconds ms) {
  eventBase_.runAfterDelay(std::move(f), ms.count());
}

void CompressionSimulator::sendAck(CompressionScheme* scheme,
                                   unique_ptr<CompressionScheme::Ack> ack) {
  if (!ack) {
    return;
  }
  scheduleEvent([a=std::move(ack), this, scheme] () mutable {
      recvAck(scheme, std::move(a));
    }, deliveryDelay());
}

void CompressionSimulator::recvAck(CompressionScheme* scheme,
                                   unique_ptr<CompressionScheme::Ack> ack) {
  scheme->recvAck(std::move(ack));
}

std::chrono::milliseconds CompressionSimulator::deliveryDelay() {
  std::chrono::milliseconds delay = one_half_rtt();
  while (loss()) {
    stats_.packetLosses++;
    scheduleEvent([] {
        VLOG(4) << "Packet lost!";
      }, delay);
    delay += rxmitDelay();
    scheduleEvent([] {
        VLOG(4) << "Packet loss detected, retransmitting";
      }, delay - one_half_rtt());
  }
  if (delayed()) {
    scheduleEvent([] {
        VLOG(4) << "Packet delayed in network";
      }, delay);
    delay += extraDelay();
  }
  return delay;
}

std::chrono::milliseconds CompressionSimulator::rtt() {
  return params_.rtt;
}

std::chrono::milliseconds CompressionSimulator::one_half_rtt() {
  return params_.rtt / 2;
}

std::chrono::milliseconds CompressionSimulator::rxmitDelay() {
  uint32_t ms = rtt().count() * Random::randDouble(1.1, 2, rng_);
  return std::chrono::milliseconds(ms);
}

bool CompressionSimulator::loss() {
  return Random::randDouble01(rng_) < params_.lossProbability;
}

bool CompressionSimulator::delayed() {
  return Random::randDouble01(rng_) < params_.delayProbability;
}

std::chrono::milliseconds CompressionSimulator::extraDelay() {
  uint32_t ms = params_.maxDelay.count() * Random::randDouble01(rng_);
  return std::chrono::milliseconds(ms);
}

uint32_t CompressionSimulator::minOOOThresh() {
  return params_.minOOOThresh;
}
}}
