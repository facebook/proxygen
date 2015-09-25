/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/HTTPConnector.h>

#include <wangle/ssl/SSLUtil.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/SPDYCodec.h>
#include <proxygen/lib/http/codec/experimental/HTTP2Codec.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <folly/io/async/AsyncSSLSocket.h>



using namespace folly;
using namespace std;

namespace proxygen {

namespace {

unique_ptr<HTTPCodec> makeCodec(const string& chosenProto,
                                bool forceHTTP1xCodecTo1_1) {
  auto spdyVersion = SPDYCodec::getVersion(chosenProto);
  if (spdyVersion) {
    return folly::make_unique<SPDYCodec>(TransportDirection::UPSTREAM,
                                         *spdyVersion);
  } else if (chosenProto == proxygen::http2::kProtocolString) {
    return folly::make_unique<HTTP2Codec>(TransportDirection::UPSTREAM);
  } else {
    if (!chosenProto.empty() &&
        !HTTP1xCodec::supportsNextProtocol(chosenProto)) {
      LOG(ERROR) << "Chosen upstream protocol " <<
        "\"" << chosenProto << "\" is unimplemented. " <<
        "Attempting to use HTTP/1.1";
    }

    return folly::make_unique<HTTP1xCodec>(TransportDirection::UPSTREAM,
                                           forceHTTP1xCodecTo1_1);
  }
}

}

HTTPConnector::HTTPConnector(Callback* callback,
                             folly::HHWheelTimer* timeoutSet)
    : cb_(CHECK_NOTNULL(callback)), timeoutSet_(timeoutSet) {}

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
  forceHTTP1xCodecTo1_1_ = enabled;
}

void HTTPConnector::connect(
  EventBase* eventBase,
  const folly::SocketAddress& connectAddr,
  chrono::milliseconds timeoutMs,
  const AsyncSocket::OptionMap& socketOptions,
  const folly::SocketAddress& bindAddr) {

  DCHECK(!isBusy());
  transportInfo_ = wangle::TransportInfo();
  transportInfo_.ssl = false;
  socket_.reset(new AsyncSocket(eventBase));
  connectStart_ = getCurrentTime();
  socket_->connect(this, connectAddr, timeoutMs.count(),
                   socketOptions, bindAddr);
}

void HTTPConnector::connectSSL(
  EventBase* eventBase,
  const folly::SocketAddress& connectAddr,
  const shared_ptr<SSLContext>& context,
  SSL_SESSION* session,
  chrono::milliseconds timeoutMs,
  const AsyncSocket::OptionMap& socketOptions,
  const folly::SocketAddress& bindAddr) {

  DCHECK(!isBusy());
  transportInfo_ = wangle::TransportInfo();
  transportInfo_.ssl = true;
  auto sslSock = new AsyncSSLSocket(context, eventBase);
  if (session) {
    sslSock->setSSLSession(session, true /* take ownership */);
  }
  socket_.reset(sslSock);
  connectStart_ = getCurrentTime();
  socket_->connect(this, connectAddr, timeoutMs.count(),
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
  if (transportInfo_.ssl) {
    AsyncSSLSocket* sslSocket = dynamic_cast<AsyncSSLSocket*>(socket_.get());

    const char* npnProto;
    unsigned npnProtoLen;
    sslSocket->getSelectedNextProtocol(
      reinterpret_cast<const unsigned char**>(&npnProto), &npnProtoLen);

    transportInfo_.sslNextProtocol =
        std::make_shared<std::string>(npnProto, npnProtoLen);
    transportInfo_.sslSetupTime = millisecondsSince(connectStart_);
    transportInfo_.sslCipher = sslSocket->getNegotiatedCipherName() ?
      std::make_shared<std::string>(sslSocket->getNegotiatedCipherName()) :
      nullptr;
    transportInfo_.sslVersion = sslSocket->getSSLVersion();
    transportInfo_.sslResume = wangle::SSLUtil::getResumeState(sslSocket);

    codec = makeCodec(*transportInfo_.sslNextProtocol, forceHTTP1xCodecTo1_1_);
  } else {
    codec = makeCodec(plaintextProtocol_, forceHTTP1xCodecTo1_1_);
  }

  HTTPUpstreamSession* session = new HTTPUpstreamSession(
    timeoutSet_,
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
