/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPSessionAcceptor.h>
#include <proxygen/lib/http/codec/HTTP1xCodec.h>
#include <proxygen/lib/http/codec/experimental/HTTP2Codec.h>
#include <proxygen/lib/http/session/HTTPDirectResponseHandler.h>

using folly::AsyncSocket;
using folly::SocketAddress;
using std::list;
using std::string;
using std::unique_ptr;

namespace proxygen {

const SocketAddress HTTPSessionAcceptor::unknownSocketAddress_("0.0.0.0", 0);

HTTPSessionAcceptor::HTTPSessionAcceptor(
  const AcceptorConfiguration& accConfig):
    HTTPAcceptor(accConfig),
    simpleController_(this) {
  if (!isSSL()) {
    auto version = SPDYCodec::getVersion(accConfig.plaintextProtocol);
    if (version) {
      alwaysUseSPDYVersion_ = *version;
    }
  }
}

HTTPSessionAcceptor::~HTTPSessionAcceptor() {
}

const HTTPErrorPage* HTTPSessionAcceptor::getErrorPage(
    const SocketAddress& addr) const {
  const HTTPErrorPage* errorPage = nullptr;
  if (isInternal()) {
    if (addr.isPrivateAddress()) {
      errorPage = diagnosticErrorPage_.get();
    }
  }
  if (errorPage == nullptr) {
    errorPage = defaultErrorPage_.get();
  }
  return errorPage;
}

void HTTPSessionAcceptor::onNewConnection(
  folly::AsyncSocket::UniquePtr ssock,
    const SocketAddress* peerAddress,
    const string& nextProtocol,
  const folly::TransportInfo& tinfo) {
  unique_ptr<HTTPCodec> codec;

  AsyncSocket::UniquePtr sock(dynamic_cast<AsyncSocket*>(ssock.release()));

  if (!isSSL() && alwaysUseSPDYVersion_) {
    codec = folly::make_unique<SPDYCodec>(
      TransportDirection::DOWNSTREAM,
      alwaysUseSPDYVersion_.value(),
      accConfig_.spdyCompressionLevel);
  } else if (nextProtocol.empty() ||
             HTTP1xCodec::supportsNextProtocol(nextProtocol)) {
    codec = folly::make_unique<HTTP1xCodec>(TransportDirection::DOWNSTREAM);
  } else if (auto version = SPDYCodec::getVersion(nextProtocol)) {
    codec = folly::make_unique<SPDYCodec>(
      TransportDirection::DOWNSTREAM,
      *version,
      accConfig_.spdyCompressionLevel);
  } else if (nextProtocol == "h2-14") {
    codec = folly::make_unique<HTTP2Codec>(TransportDirection::DOWNSTREAM);
  } else {
    // Either we advertised a protocol we don't support or the
    // client requested a protocol we didn't advertise.
    VLOG(2) << "Client requested unrecognized next protocol " << nextProtocol;
    return;
  }

  CHECK(codec);

  auto controller = getController();
  SocketAddress localAddress;
  try {
    sock->getLocalAddress(&localAddress);
  } catch (...) {
    VLOG(3) << "couldn't get local address for socket";
    localAddress = unknownSocketAddress_;
  }
  VLOG(4) << "Created new session for peer " << *peerAddress;
  HTTPDownstreamSession* session =
    new HTTPDownstreamSession(getTransactionTimeoutSet(), std::move(sock),
                              localAddress, *peerAddress,
                              controller, std::move(codec), tinfo, this);
  if (accConfig_.maxConcurrentIncomingStreams) {
    session->setMaxConcurrentIncomingStreams(
        accConfig_.maxConcurrentIncomingStreams);
  }
  // set flow control parameters
  session->setFlowControl(accConfig_.initialReceiveWindow,
                          accConfig_.receiveStreamWindowSize,
                          accConfig_.receiveSessionWindowSize);
  session->setSessionStats(downstreamSessionStats_);
  Acceptor::addConnection(session);
  session->startNow();
}

} // proxygen
