/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Range.h>
#include <folly/portability/Windows.h> // for windows compatibility: STRICT maybe defined by some win headers

#ifdef STRICT
#undef STRICT
#endif

#ifdef STRICT_COMPAT
#undef STRICT_COMPAT
#endif

namespace proxygen {

// Case-insensitive string comparison
inline bool caseInsensitiveEqual(folly::StringPiece s, folly::StringPiece t) {
  return s.equals(t, folly::AsciiCaseInsensitive{});
}

struct AsciiCaseUnderscoreInsensitive {
  bool operator()(char lhs, char rhs) const {
    if (lhs == '_') {
      lhs = '-';
    }
    if (rhs == '_') {
      rhs = '-';
    }
    return folly::AsciiCaseInsensitive()(lhs, rhs);
  }
};

// Case-insensitive string comparison
inline bool caseUnderscoreInsensitiveEqual(folly::StringPiece s,
                                           folly::StringPiece t) {
  return s.equals(t, AsciiCaseUnderscoreInsensitive{});
}

enum class URLValidateMode : uint8_t { STRICT_COMPAT, STRICT };
inline bool validateURL(std::string_view url,
                        URLValidateMode mode = URLValidateMode::STRICT) {
  // No controls or unescaped spaces. Each loop has no early return and no
  // short-circuit operators, so the compiler can vectorize it. Each mode keeps
  // its own result: one shared across both loops leaves the first one scalar.
  if (mode == URLValidateMode::STRICT_COMPAT) {
    bool valid = true;
    for (uint8_t p : url) {
      valid &= (p > 0x20) & (p != 0x7f);
    }
    return valid;
  }
  bool valid = true;
  for (uint8_t p : url) {
    valid &= (p > 0x20) & (p < 0x7f);
  }
  return valid;
}

inline size_t findLastOf(folly::StringPiece sp, char c) {
  size_t pos = sp.size();
  while (--pos != std::string::npos && sp[pos] != c) {
    // pass
  }
  return pos;
}

template <typename Tout, typename Tin>
Tout clamped_downcast(Tin value) {
  return static_cast<Tout>(
      std::min(static_cast<uint64_t>(value),
               static_cast<uint64_t>(std::numeric_limits<Tout>::max())));
}

// Like std::isalpha but independent of the current locale.
inline bool isAlpha(uint8_t c) {
  return ((unsigned int)(c | 0x20) - 'a') < 26U;
}

} // namespace proxygen
