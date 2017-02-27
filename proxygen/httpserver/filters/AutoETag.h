#pragma once

#include <folly/SpookyHashV2.h>

#include <proxygen/httpserver/Filters.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>

namespace proxygen {

/**
 * AutoETag generates ETags for responses and sends back 304 Not Modified
 * responses to requestors who already have an up-to-date copy of the response.
 * This can save bandwidth and reduce round-trip-time when reloading an object
 * that is unchanged.
 *
 * It operates by caching the entire response in memory and hashing it before
 * sending the response. Once the ETag has been calculated, the request is
 * checked for If-None-Match conditional request headers. If any of the
 * entity-tags in the If-None-Match header match, a 304 Not Modified response
 * is sent and the message body is dropped.
 *
 * If your objects are large enough that caching them entirely in memory before
 * sending the response is excessive, it's best to calculate your own ETag
 * and include it in sendHeaders(). In that case AutoETag will provide the
 * conditonal response behavior as above, but if the client request does not
 * match the ETag then AutoETag removes itself from the response chain
 * altogether.
 *
 * AutoETag will also be disabled for responses that are explicitly chunked by
 * preceding handlers to avoid delaying delivery of each chunk.
 *
 * Usage:
 * There are two ways to use the AutoETag filter. Either globally for all
 * handlers or on a per-handler basis.
 *
 * To enable AutoETag on all requests, add it to the handler factory chain:
 *   HTTPOptions options;
 *   options.handlerFactories = RequestHandlerChain()
 *     .addThen<AutoETagFilterFactory>()
 *     .addThen<MyHandlerFilterFactory()
 *     .build();
 *
 * To enable AutoETag on a per-handler basis, add it after constructing your
 * handler in the handler factory:
 *
 *   MyHandlerFactory::onRequest() {
 *     auto myHandler = new MyHandler();
 *     return new proxygen::AutoETag(myHandler);
 *   }
 */

class AutoETag : public Filter {
public:
  AutoETag(RequestHandler* upstream);

  void onRequest(std::unique_ptr<HTTPMessage> headers) noexcept override;

  void sendHeaders(HTTPMessage& msg) noexcept override;
  void sendBody(std::unique_ptr<folly::IOBuf> body) noexcept override;
  void sendEOM() noexcept override;

 protected:
  static bool etagMatches(const std::string& etag, const std::vector<std::string>& etags) noexcept;

  virtual void send304NotModified(const std::string& etag) noexcept;

 private:
  // Request
  std::vector<std::string> if_none_match_;

  // Response
  HTTPMessage msg_;
  std::unique_ptr<folly::IOBuf> body_;

  folly::hash::SpookyHashV2 hasher_;
};

class AutoETagFilterFactory : public RequestHandlerFactory {
 public:
  void onServerStart(folly::EventBase* evb) noexcept override {}
  void onServerStop() noexcept override {}

  RequestHandler* onRequest(RequestHandler* h, HTTPMessage* msg) noexcept override;
};

}
