/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/httpserver/RequestHandlerAdaptor.h>

#include <boost/algorithm/string.hpp>
#include <proxygen/httpserver/PushHandler.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>

namespace proxygen {

RequestHandlerAdaptor::RequestHandlerAdaptor(RequestHandler* requestHandler)
    : ResponseHandler(requestHandler) {
}

void RequestHandlerAdaptor::setTransaction(HTTPTransaction* txn) noexcept {
  txn_ = txn;

  // We become that transparent layer
  upstream_->setResponseHandler(this);
}

void RequestHandlerAdaptor::detachTransaction() noexcept {
  if (err_ == kErrorNone) {
    upstream_->requestComplete();
  }

  // Otherwise we would have got some error call back and invoked onError
  // on RequestHandler
  delete this;
}

void RequestHandlerAdaptor::onHeadersComplete(std::unique_ptr<HTTPMessage> msg)
    noexcept {
  if (msg->getHeaders().exists(HTTP_HEADER_EXPECT)) {
    auto expectation = msg->getHeaders().getSingleOrEmpty(HTTP_HEADER_EXPECT);
    if (!boost::iequals(expectation, "100-continue")) {
      setError(kErrorUnsupportedExpectation);

      ResponseBuilder(this)
          .status(417, "Expectation Failed")
          .closeConnection()
          .sendWithEOM();
    } else {
      ResponseBuilder(this)
        .status(100, "Continue")
        .send();

    }
  }

  // Only in case of no error
  if (err_ == kErrorNone) {
    upstream_->onRequest(std::move(msg));
  }
}

void RequestHandlerAdaptor::onBody(std::unique_ptr<folly::IOBuf> c) noexcept {
  upstream_->onBody(std::move(c));
}

void RequestHandlerAdaptor::onChunkHeader(size_t length) noexcept {
}

void RequestHandlerAdaptor::onChunkComplete() noexcept {
}

void RequestHandlerAdaptor::onTrailers(std::unique_ptr<HTTPHeaders> trailers)
    noexcept {
  // XXX: Support trailers
}

void RequestHandlerAdaptor::onEOM() noexcept {
  if (err_ == kErrorNone) {
    upstream_->onEOM();
  }
}

void RequestHandlerAdaptor::onUpgrade(UpgradeProtocol protocol) noexcept {
  upstream_->onUpgrade(protocol);
}

void RequestHandlerAdaptor::onError(const HTTPException& error) noexcept {
  if (err_ != kErrorNone) {
    // we have already handled an error and upstream would have been deleted
    return;
  }

  if (error.getProxygenError() == kErrorTimeout) {
    setError(kErrorTimeout);

    if (responseStarted_) {
      sendAbort();
    } else {
      ResponseBuilder(this)
          .status(408, "Request Timeout")
          .closeConnection()
          .sendWithEOM();
    }
  } else if (error.getDirection() == HTTPException::Direction::INGRESS) {
    setError(kErrorRead);

    if (responseStarted_) {
      sendAbort();
    } else {
      ResponseBuilder(this)
          .status(400, "Bad Request")
          .closeConnection()
          .sendWithEOM();
    }

  } else {
    setError(kErrorWrite);
  }

  // Wait for detachTransaction to clean up
}

void RequestHandlerAdaptor::onEgressPaused() noexcept {
  upstream_->onEgressPaused();
}

void RequestHandlerAdaptor::onEgressResumed() noexcept {
  upstream_->onEgressResumed();
}

void RequestHandlerAdaptor::sendHeaders(HTTPMessage& msg) noexcept {
  responseStarted_ = true;
  txn_->sendHeaders(msg);
}

void RequestHandlerAdaptor::sendChunkHeader(size_t len) noexcept {
  txn_->sendChunkHeader(len);
}

void RequestHandlerAdaptor::sendBody(std::unique_ptr<folly::IOBuf> b) noexcept {
  txn_->sendBody(std::move(b));
}

void RequestHandlerAdaptor::sendChunkTerminator() noexcept {
  txn_->sendChunkTerminator();
}

void RequestHandlerAdaptor::sendEOM() noexcept {
  txn_->sendEOM();
}

void RequestHandlerAdaptor::sendAbort() noexcept {
  txn_->sendAbort();
}

void RequestHandlerAdaptor::refreshTimeout() noexcept {
  txn_->refreshTimeout();
}

void RequestHandlerAdaptor::pauseIngress() noexcept {
  txn_->pauseIngress();
}

void RequestHandlerAdaptor::resumeIngress() noexcept {
  txn_->resumeIngress();
}

ResponseHandler* RequestHandlerAdaptor::newPushedResponse(
  PushHandler* pushHandler) noexcept {
  auto pushTxn = txn_->newPushedTransaction(pushHandler->getHandler());
  if (!pushTxn) {
    // Codec doesn't support push
    return nullptr;;
  }
  auto pushHandlerAdaptor = new RequestHandlerAdaptor(pushHandler);
  pushHandlerAdaptor->setTransaction(pushTxn);
  return pushHandlerAdaptor;
}

const wangle::TransportInfo&
RequestHandlerAdaptor::getSetupTransportInfo() const noexcept {
  return txn_->getSetupTransportInfo();
}

void RequestHandlerAdaptor::getCurrentTransportInfo(
  wangle::TransportInfo* tinfo) const {
  txn_->getCurrentTransportInfo(tinfo);
}

void RequestHandlerAdaptor::setError(ProxygenError err) noexcept {
  err_ = err;
  upstream_->onError(err);
}

}
