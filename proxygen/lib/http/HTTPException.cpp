/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/HTTPException.h>

namespace proxygen {

std::string HTTPException::describe() const {
  std::stringstream ss;
  ss << *this;
  return ss.str();
}

std::ostream& operator<<(std::ostream& os, const HTTPException& ex) {
  os << "what=\"" << ex.what()
     << "\", direction=" << static_cast<int>(ex.getDirection())
     << ", proxygenError=" << getErrorString(ex.getProxygenError())
     << ", codecStatusCode=" << (ex.hasCodecStatusCode() ?
                                 getErrorCodeString(ex.getCodecStatusCode()) :
                                 "-1")
     << ", httpStatusCode=" << ex.getHttpStatusCode();
  return os;
}

}
