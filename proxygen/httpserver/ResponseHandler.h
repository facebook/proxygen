/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/session/HTTPTransaction.h>

namespace proxygen {

class RequestHandler;
class PushHandler;

/**
 * Interface that acts as client for RequestHandler. It also has a hook
 * for the RequestHandler so that it is easy to chain these Request/Response
 * handlers and be able to modify these chains.
 *
 * The names are pretty much self explanatory. You only need
 * to get into details about this interface if you are implementing filters.
 *
 * NOTE: All the writes are done at the end of the event loop. So this is safe
 *       to do in your RequestHandler.
 *
 *       {
 *         ...
 *         downstream_->sendHeader(...);
 *         downstream_->sendEOM();
 *       }
 *
 *       You dont need to worry about any callbacks being invoked after
 *       sendHeader.
 *
 *       Consider using proxygen/httpserver/ResponseBuilder to send back the
 *       response. It will take care of chunking response if required and
 *       everything.
 */
class ResponseHandler {
 public:
  explicit ResponseHandler(RequestHandler* upstream)
      : upstream_(CHECK_NOTNULL(upstream)) {
  }

  virtual ~ResponseHandler() {}

  /**
   * NOTE: We take response message as non-const reference, to allow filters
   *       between your handler and client to be able to modify response
   *       if they want to.
   *
   *       eg. a compression filter might want to change the content-encoding
   */
  virtual void sendHeaders(HTTPMessage& msg) noexcept = 0;

  virtual void sendChunkHeader(size_t len) noexcept = 0;

  virtual void sendBody(std::unique_ptr<folly::IOBuf> body) noexcept = 0;

  virtual void sendChunkTerminator() noexcept = 0;

  virtual void sendEOM() noexcept = 0;

  virtual void sendAbort() noexcept = 0;

  virtual void refreshTimeout() noexcept = 0;

  virtual void pauseIngress() noexcept = 0;

  virtual void resumeIngress() noexcept = 0;

  virtual ResponseHandler* newPushedResponse(
    PushHandler* pushHandler) noexcept = 0;

  // Accessors for Transport/Connection information
  virtual const wangle::TransportInfo& getSetupTransportInfo() const noexcept = 0;

  virtual void getCurrentTransportInfo(wangle::TransportInfo* tinfo) const = 0;

 protected:
  RequestHandler* upstream_{nullptr};
};

}
