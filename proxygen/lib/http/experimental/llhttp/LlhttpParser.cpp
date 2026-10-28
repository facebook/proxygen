/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "proxygen/lib/http/experimental/llhttp/LlhttpParser.h"

#include <cassert>

namespace facebook::detail {

LlhttpParser::LlhttpParser(llhttp_type_t type,
                           const llhttp_settings_t* settings,
                           void* callbackContext) {
  assert(settings != nullptr);
  llhttp_init(&parser_, type, settings);
  parser_.data = callbackContext;
}

size_t LlhttpParser::execute(const char* data, size_t len) {
  if (error() != HPE_OK) {
    return 0;
  }

  active_ = true;
  const auto executeError = llhttp_execute(&parser_, data, len);
  active_ = false;
  if (executeError == HPE_OK) {
    return len;
  }

  return static_cast<size_t>(llhttp_get_error_pos(&parser_) - data);
}

void LlhttpParser::finish() {
  // Don't finish if the parser is already in an error state
  if (hasFatalError()) {
    return;
  }
  active_ = true;
  const auto err = llhttp_finish(&parser_);
  active_ = false;
  if (err != HPE_OK) {
    parser_.error = err;
  }
}

void LlhttpParser::setError(llhttp_errno_t error, const char* reason) {
  assert(error != HPE_OK);
  assert(reason != nullptr);

  parser_.error = static_cast<int32_t>(error);
  llhttp_set_error_reason(&parser_, reason);
}

void LlhttpParser::pause() {
  llhttp_pause(&parser_);
}

void LlhttpParser::resume() {
  llhttp_resume(&parser_);
}

void LlhttpParser::resumeAfterUpgrade() {
  llhttp_resume_after_upgrade(&parser_);
}

const char* LlhttpParser::errorReason() const {
  return llhttp_get_error_reason(&parser_);
}

const char* LlhttpParser::methodStr() const {
  return llhttp_method_name(static_cast<llhttp_method_t>(parser_.method));
}

} // namespace facebook::detail
