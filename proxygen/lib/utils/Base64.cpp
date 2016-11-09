/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/utils/Base64.h>
#include <string.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <folly/Range.h>

namespace {
// Calculates the length of a decoded string
size_t calcDecodeLength(const char* b64input) {
  size_t len = strlen(b64input);
  size_t padding = 0;

  if (len > 2 && b64input[len - 1] == '=' && b64input[len - 2] == '=') {
    //last two chars are =
    padding = 2;
  } else if (len > 1 && b64input[len - 1] == '=') {
    //last char is =
    padding = 1;
  } // else no padding

  return (len * 3) / 4 - padding;
}

struct BIODeleter {
 public:
  void operator()(BIO* bio) const {
    BIO_free_all(bio);
  };
};

}

namespace proxygen {

// Decodes a base64 encoded string
std::string Base64::decode(const std::string& b64message) {
  std::unique_ptr<BIO, BIODeleter> bio, b64;

  int decodeLen = calcDecodeLength(b64message.c_str());
  std::string result(decodeLen, '\0');

  bio.reset(BIO_new_mem_buf((void*)b64message.data(), -1));
  if (!bio) {
    return std::string();
  }
  b64.reset(BIO_new(BIO_f_base64()));
  if (!b64) {
    return std::string();
  }
  bio.reset(BIO_push(b64.release(), bio.release()));

  // Do not use newlines to flush buffer
  BIO_set_flags(bio.get(), BIO_FLAGS_BASE64_NO_NL);
  auto length = BIO_read(bio.get(), (char *)result.data(), b64message.length());
  DCHECK_LE(length, decodeLen);
  if (length < decodeLen) {
    return std::string();
  }

  return result;
}

// Encodes a binary safe base 64 string
std::string Base64::encode(folly::ByteRange buffer) {
  std::unique_ptr<BIO, BIODeleter> bio, b64;
  BUF_MEM *bufferPtr;

  b64.reset(BIO_new(BIO_f_base64()));
  if (!b64) {
    throw std::bad_alloc();
  }
  bio.reset(BIO_new(BIO_s_mem()));
  if (!bio) {
    throw std::bad_alloc();
  }
  bio.reset(BIO_push(b64.release(), bio.release()));

  // Ignore newlines - write everything in one line
  BIO_set_flags(bio.get(), BIO_FLAGS_BASE64_NO_NL);
  BIO_write(bio.get(), buffer.data(), buffer.size());
  BIO_flush(bio.get());
  BIO_get_mem_ptr(bio.get(), &bufferPtr);
  BIO_set_close(bio.get(), BIO_NOCLOSE);

  auto result = std::string(bufferPtr->data, bufferPtr->length);
  BUF_MEM_free(bufferPtr);
  return result;
}
}
