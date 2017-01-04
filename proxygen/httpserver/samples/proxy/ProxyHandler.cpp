/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ProxyHandler.h"

#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <proxygen/lib/utils/URL.h>
#include <gflags/gflags.h>
#include <folly/io/async/EventBaseManager.h>

#include "ProxyStats.h"

using namespace proxygen;
using std::string;
using std::unique_ptr;

DEFINE_int32(proxy_connect_timeout, 1000,
    "connect timeout in milliseconds");

namespace ProxyService {

ProxyHandler::ProxyHandler(ProxyStats* stats, folly::HHWheelTimer* timer):
    stats_(stats),
    connector_{this, timer},
    serverHandler_(*this) {
}

ProxyHandler::~ProxyHandler() {
}

void ProxyHandler::onRequest(std::unique_ptr<HTTPMessage> headers) noexcept {
  // This HTTP proxy does not obey the rules in the spec, such as stripping
  // hop-by-hop headers.  Example only!

  stats_->recordRequest();
  request_ = std::move(headers);
  proxygen::URL url(request_->getURL());

  folly::SocketAddress addr;
  try {
    // Note, this does a synchronous DNS lookup which is bad in event driven
    // code
    addr.setFromHostPort(url.getHost(), url.getPort());
  } catch (...) {
    ResponseBuilder(downstream_)
      .status(503, "Bad Gateway")
      .body(folly::to<string>("Could not parse server from URL: ",
                              request_->getURL()))
      .sendWithEOM();
    return;
  }

  // A more sophisticated proxy would have a connection pool here

  LOG(INFO) << "Trying to connect to " << addr;
  const folly::AsyncSocket::OptionMap opts{
    {{SOL_SOCKET, SO_REUSEADDR}, 1}};
  downstream_->pauseIngress();
  connector_.connect(folly::EventBaseManager::get()->getEventBase(), addr,
        std::chrono::milliseconds(FLAGS_proxy_connect_timeout), opts);
}

void ProxyHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
  if (txn_) {
    LOG(INFO) << "Forwarding " <<
      ((body) ? body->computeChainDataLength() : 0) << " body bytes to server";
    txn_->sendBody(std::move(body));
  } else {
    LOG(WARNING) << "Dropping " <<
      ((body) ? body->computeChainDataLength() : 0) << " body bytes to server";
  }
}

void ProxyHandler::onEOM() noexcept {
  if (txn_) {
    LOG(INFO) << "Forwarding client EOM to server";
    txn_->sendEOM();
  } else {
    LOG(INFO) << "Dropping client EOM to server";
  }
}

void ProxyHandler::connectSuccess(HTTPUpstreamSession* session) {
  LOG(INFO) << "Established " << *session;
  session_ = folly::make_unique<SessionWrapper>(session);
  txn_ = session->newTransaction(&serverHandler_);
  LOG(INFO) << "Forwarding client request: " << request_->getURL()
            << " to server";
  txn_->sendHeaders(*request_);
  downstream_->resumeIngress();
}

void ProxyHandler::connectError(const folly::AsyncSocketException& ex) {
  LOG(ERROR) << "Failed to connect: " << folly::exceptionStr(ex);
  if (!clientTerminated_) {
    ResponseBuilder(downstream_)
      .status(503, "Bad Gateway")
      .sendWithEOM();
  } else {
    checkForShutdown();
  }
}

void ProxyHandler::onServerHeadersComplete(
  unique_ptr<HTTPMessage> msg) noexcept {
  CHECK(!clientTerminated_);
  LOG(INFO) << "Forwarding " << msg->getStatusCode() << " response to client";
  downstream_->sendHeaders(*msg);
}

void ProxyHandler::onServerBody(std::unique_ptr<folly::IOBuf> chain) noexcept {
  CHECK(!clientTerminated_);
  LOG(INFO) << "Forwarding " <<
    ((chain) ? chain->computeChainDataLength() : 0) << " body bytes to client";
  downstream_->sendBody(std::move(chain));
}

void ProxyHandler::onServerEOM() noexcept {
  CHECK(!clientTerminated_);
  LOG(INFO) << "Forwarding server EOM to client";
  downstream_->sendEOM();
}

void ProxyHandler::detachServerTransaction() noexcept {
  txn_ = nullptr;
  checkForShutdown();
}

void ProxyHandler::onServerError(const HTTPException& error) noexcept {
  LOG(ERROR) << "Server error: " << error;
  downstream_->sendAbort();
}

void ProxyHandler::onServerEgressPaused() noexcept {
  downstream_->pauseIngress();
}

void ProxyHandler::onServerEgressResumed() noexcept {
  downstream_->resumeIngress();
}

void ProxyHandler::requestComplete() noexcept {
  clientTerminated_ = true;
  checkForShutdown();
}

void ProxyHandler::onError(ProxygenError err) noexcept {
  LOG(ERROR) << "Client error: " << proxygen::getErrorString(err);
  if (txn_) {
    LOG(ERROR) << "Aborting server txn: " << *txn_;
    txn_->sendAbort();
  }
  clientTerminated_ = true;
  checkForShutdown();
}

void ProxyHandler::onEgressPaused() noexcept {
  if (txn_) {
    txn_->pauseIngress();
  }
}

void ProxyHandler::onEgressResumed() noexcept {
  if (txn_) {
    txn_->resumeIngress();
  }
}

bool ProxyHandler::checkForShutdown() {
  if (clientTerminated_ && !txn_) {
    delete this;
    return true;
  }
  return false;
}

}
