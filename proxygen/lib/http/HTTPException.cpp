/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <sstream>
#include <string>

#include <proxygen/lib/http/HTTPException.h>

namespace proxygen {

HTTPException::HTTPException(Direction dir, const std::string& msg)
    : Exception(msg), dir_(dir) {
}

HTTPException::HTTPException(Direction dir, const char* msg)
    : Exception(msg), dir_(dir) {
}

HTTPException::HTTPException(const HTTPException& ex)
    : Exception(static_cast<const Exception&>(ex)),
      dir_(ex.dir_),
      httpStatusCode_(ex.httpStatusCode_),
      codecStatusCode_(ex.codecStatusCode_),
      errno_(ex.errno_) {
  if (ex.currentIngressBuf_) {
    currentIngressBuf_ = ex.currentIngressBuf_->clone();
  }
  if (ex.partialMsg_) {
    partialMsg_ = std::make_unique<HTTPMessage>(*ex.partialMsg_.get());
  }
}

std::string HTTPException::describe() const {
  std::stringstream ss;
  ss << *this;
  return ss.str();
}

std::ostream& operator<<(std::ostream& os, const HTTPException& ex) {
  os << "what=\"" << ex.what()
     << "\", direction=" << static_cast<int>(ex.getDirection())
     << ", proxygenError=" << getErrorString(ex.getProxygenError())
     << ", codecStatusCode="
     << (ex.hasCodecStatusCode() ? getErrorCodeString(ex.getCodecStatusCode())
                                 : "-1")
     << ", httpStatusCode=" << ex.getHttpStatusCode();
  return os;
}

} // namespace proxygen
