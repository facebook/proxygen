/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/HTTPConnector.h>

#include <wangle/ssl/SSLUtil.h>
#include <proxygen/lib/http/codec/DefaultHTTPCodecFactory.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/SPDYCodec.h>
#include <proxygen/lib/http/codec/HTTP2Codec.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <folly/io/async/AsyncSSLSocket.h>


using namespace folly;
using namespace std;

namespace proxygen {

HTTPConnector::HTTPConnector(Callback* callback,
    folly::HHWheelTimer* timeoutSet)
    : HTTPConnector(callback, WheelTimerInstance(timeoutSet)) {
}

HTTPConnector::HTTPConnector(Callback* callback,
                             const WheelTimerInstance& timeout)
    : cb_(CHECK_NOTNULL(callback))
    , timeout_(timeout)
    , httpCodecFactory_(std::make_unique<DefaultHTTPCodecFactory>(false)) {}

HTTPConnector::~HTTPConnector() {
  reset();
}

void HTTPConnector::reset() {
  if (socket_) {
    auto cb = cb_;
    cb_ = nullptr;
    socket_.reset(); // This invokes connectError() but will be ignored
    cb_ = cb;
  }
}

void HTTPConnector::setPlaintextProtocol(const std::string& plaintextProto) {
  plaintextProtocol_ = plaintextProto;
}

void HTTPConnector::setHTTPVersionOverride(bool enabled) {
  httpCodecFactory_->setForceHTTP1xCodecTo1_1(enabled);
}

void HTTPConnector::connect(
  EventBase* eventBase,
  const folly::SocketAddress& connectAddr,
  std::chrono::milliseconds timeoutMs,
  const AsyncSocket::OptionMap& socketOptions,
  const folly::SocketAddress& bindAddr) {

  DCHECK(!isBusy());
  transportInfo_ = wangle::TransportInfo();
  transportInfo_.secure = false;
  auto sock = new AsyncSocket(eventBase);
  socket_.reset(sock);
  connectStart_ = getCurrentTime();
  sock->connect(this, connectAddr, timeoutMs.count(),
                   socketOptions, bindAddr);
}

void HTTPConnector::connectSSL(
  EventBase* eventBase,
  const folly::SocketAddress& connectAddr,
  const shared_ptr<SSLContext>& context,
  SSL_SESSION* session,
  std::chrono::milliseconds timeoutMs,
  const AsyncSocket::OptionMap& socketOptions,
  const folly::SocketAddress& bindAddr,
  const std::string& serverName) {

  DCHECK(!isBusy());
  transportInfo_ = wangle::TransportInfo();
  transportInfo_.secure = true;
  auto sslSock = new AsyncSSLSocket(context, eventBase);
  if (session) {
    sslSock->setSSLSession(session, true /* take ownership */);
  }
  sslSock->setServerName(serverName);
  sslSock->forceCacheAddrOnFailure(true);
  socket_.reset(sslSock);
  connectStart_ = getCurrentTime();
  sslSock->connect(this, connectAddr, timeoutMs.count(),
                   socketOptions, bindAddr);
}

std::chrono::milliseconds HTTPConnector::timeElapsed() {
  if (timePointInitialized(connectStart_)) {
    return millisecondsSince(connectStart_);
  }
  return std::chrono::milliseconds(0);
}

// Callback interface

void HTTPConnector::connectSuccess() noexcept {
  if (!cb_) {
    return;
  }

  folly::SocketAddress localAddress;
  folly::SocketAddress peerAddress;
  socket_->getLocalAddress(&localAddress);
  socket_->getPeerAddress(&peerAddress);

  std::unique_ptr<HTTPCodec> codec;

  transportInfo_.acceptTime = getCurrentTime();
  if (transportInfo_.secure) {
    AsyncSSLSocket* sslSocket = socket_->getUnderlyingTransport<AsyncSSLSocket>();

    if (sslSocket) {
      transportInfo_.appProtocol =
          std::make_shared<std::string>(socket_->getApplicationProtocol());
      transportInfo_.sslSetupTime = millisecondsSince(connectStart_);
      transportInfo_.sslCipher = sslSocket->getNegotiatedCipherName() ?
        std::make_shared<std::string>(sslSocket->getNegotiatedCipherName()) :
        nullptr;
      transportInfo_.sslVersion = sslSocket->getSSLVersion();
      transportInfo_.sslResume = wangle::SSLUtil::getResumeState(sslSocket);
    }
    codec = httpCodecFactory_->getCodec(socket_->getApplicationProtocol(),
                                        TransportDirection::UPSTREAM, true);
  } else {
    codec = httpCodecFactory_->getCodec(plaintextProtocol_,
                                        TransportDirection::UPSTREAM, false);
  }

  HTTPUpstreamSession* session = new HTTPUpstreamSession(
    timeout_,
    std::move(socket_), localAddress, peerAddress,
    std::move(codec), transportInfo_, nullptr);

  cb_->connectSuccess(session);
}

void HTTPConnector::connectErr(const AsyncSocketException& ex) noexcept {
  socket_.reset();
  if (cb_) {
    cb_->connectError(ex);
  }
}

}
