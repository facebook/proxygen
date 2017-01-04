/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "PushRequestHandler.h"

#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <proxygen/httpserver/PushHandler.h>
#include <folly/FileUtil.h>
#include "PushStats.h"

using namespace proxygen;

namespace PushService {

const std::string kPushFileName("proxygen/httpserver/samples/push/pusheen.txt");
std::string gPushBody;

// Create a large body so we can give time for the push request to be made
std::string createLargeBody() {
  std::string data = gPushBody;
  while(data.size() < 1000*1000) {
    data += gPushBody;
  }
  return data;
}

std::string generateUrl(const HTTPMessage& message, const char* path) {
  return HTTPMessage::createUrl(
      message.isSecure() ? "https" : "http",
      message.getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST), path, "", "");
}

PushRequestHandler::PushRequestHandler(PushStats* stats) : stats_(stats) {
  if (gPushBody.empty()) {
    CHECK(folly::readFile(kPushFileName.c_str(), gPushBody))
      << "Failed to read push file=" << kPushFileName;
  }
}

void PushRequestHandler::onRequest(
  std::unique_ptr<HTTPMessage> headers) noexcept {
  stats_->recordRequest();
  if (!headers->getHeaders().getSingleOrEmpty("X-PushIt").empty()) {
    downstreamPush_ = downstream_->newPushedResponse(new PushHandler);
    if (!downstreamPush_) {
      // can't push
      return;
    }

    if(headers->getPath() == "/requestLargePush") {
      LOG(INFO) << "sending large push ";

      ResponseBuilder(downstreamPush_)
        .promise(generateUrl(*headers, "/largePush"),
                 headers->getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST))
        .send();

      ResponseBuilder(downstreamPush_)
        .status(200, "OK")
        .body(createLargeBody())
        .sendWithEOM();
    } else {
      LOG(INFO) << "sending small push ";

      ResponseBuilder(downstreamPush_)
        .promise(generateUrl(*headers, "/pusheen"),
                 headers->getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST))
        .send();

      ResponseBuilder(downstreamPush_)
        .status(200, "OK")
        .body(gPushBody)
        .sendWithEOM();
    }
  }
}

void PushRequestHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
  if (body_) {
    body_->prependChain(std::move(body));
  } else {
    body_ = std::move(body);
  }
}

void PushRequestHandler::onEOM() noexcept {
  ResponseBuilder(downstream_)
    .status(200, "OK")
    .header("Request-Number",
            folly::to<std::string>(stats_->getRequestCount()))
    .body(std::move(body_))
    .sendWithEOM();
}

void PushRequestHandler::onUpgrade(UpgradeProtocol protocol) noexcept {
  // handler doesn't support upgrades
}

void PushRequestHandler::requestComplete() noexcept {
  delete this;
}

void PushRequestHandler::onError(ProxygenError err) noexcept {
  delete this;
}

}
