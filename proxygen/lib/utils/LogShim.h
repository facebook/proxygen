/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <proxygen/proxygen-config.h>

/**
 * proxygen/lib/utils/LogShim.h defines the PRX_LOG, PRX_VLOG, PRX_CHECK,
 * PRX_DCHECK, and PRX_PCHECK macros.
 *
 * The backend is selected by exactly one of PRX_LOGGING_GLOG,
 * PRX_LOGGING_XLOG, or PRX_LOGGING_DISABLED being defined to 1
 * in <proxygen/proxygen-config.h>. CMake derives that from
 * -DPROXYGEN_LOGGING_BACKEND ({GLOG,XLOG,DISABLED}); Buck consumers select
 * the variant via `-c proxygen.logging_backend={glog,xlog,disabled}` (default
 * glog), which the //proxygen/lib/utils:config rule reads to pick the
 * appropriate hand-written header.
 *
 * Macro authoring constraints:
 *   - PRX_LOG(level): `level` must be one of the unqualified glog tokens
 *     INFO, WARNING, ERROR, FATAL, DFATAL.
 *   - PRX_VLOG(n): `n` must be an integer literal 0..9. Values >9 are
 *     accepted (saturated to DBG9 in folly-logging mode) but for granularity
 *     finer than 0..9, use the appropriate PRX_LOG level instead.
 *   - PRX_PCHECK loses the `: <strerror(errno)>` suffix in folly-logging
 *     and disabled modes. TODO: fix once folly exposes XPCHECK.
 */

#if PRX_LOGGING_GLOG

#include <folly/GLog.h>
#define PRX_VLOG VLOG
#define PRX_VLOG_LEVEL VLOG
#define PRX_VLOG_IF VLOG_IF
#define PRX_VLOG_IS_ON VLOG_IS_ON
#define PRX_DLOG DLOG
#define PRX_DVLOG DVLOG
#define PRX_DVLOG_LEVEL DVLOG
#define PRX_LOG LOG
#define PRX_LOG_IF LOG_IF
#define PRX_LOG_EVERY_MS FB_LOG_EVERY_MS
#define PRX_LOG_EVERY_N LOG_EVERY_N
#define PRX_CHECK CHECK
#define PRX_CHECK_EQ CHECK_EQ
#define PRX_CHECK_NE CHECK_NE
#define PRX_CHECK_GE CHECK_GE
#define PRX_CHECK_GT CHECK_GT
#define PRX_CHECK_LE CHECK_LE
#define PRX_CHECK_LT CHECK_LT
#define PRX_CHECK_NOTNULL CHECK_NOTNULL
#define PRX_PCHECK PCHECK
#define PRX_DCHECK DCHECK
#define PRX_DCHECK_EQ DCHECK_EQ
#define PRX_DCHECK_NE DCHECK_NE
#define PRX_DCHECK_GE DCHECK_GE
#define PRX_DCHECK_GT DCHECK_GT
#define PRX_DCHECK_LE DCHECK_LE
#define PRX_DCHECK_LT DCHECK_LT

#elif PRX_LOGGING_XLOG

#include <folly/logging/LogLevel.h>
#include <folly/logging/Logger.h>
#include <folly/logging/xlog.h>

// Mapping from glog level tokens to the corresponding folly::xlog values.
#define PRX_LOGLEVEL_INFO INFO
#define PRX_LOGLEVEL_WARNING WARN
#define PRX_LOGLEVEL_ERROR ERR
#define PRX_LOGLEVEL_FATAL FATAL
#define PRX_LOGLEVEL_DFATAL DFATAL

// glog's VLOG(n) accepts arbitrary integer verbosity; folly's xlog only
// defines DBG0..DBG9. Map 0..9 directly; saturate higher values to DBG9 so
// VLOG(10+) call sites still compile in folly-logging mode. The cap is
// documented at the top of this header.
#define PRX_LOGGING_DBG_0 DBG0
#define PRX_LOGGING_DBG_1 DBG1
#define PRX_LOGGING_DBG_2 DBG2
#define PRX_LOGGING_DBG_3 DBG3
#define PRX_LOGGING_DBG_4 DBG4
#define PRX_LOGGING_DBG_5 DBG5
#define PRX_LOGGING_DBG_6 DBG6
#define PRX_LOGGING_DBG_7 DBG7
#define PRX_LOGGING_DBG_8 DBG8
#define PRX_LOGGING_DBG_9 DBG9
#define PRX_LOGGING_DBG_10 DBG9
#define PRX_LOGGING_DBG_11 DBG9
#define PRX_LOGGING_DBG_12 DBG9
#define PRX_LOGGING_DBG_13 DBG9
#define PRX_LOGGING_DBG_14 DBG9
#define PRX_LOGGING_DBG_15 DBG9

#define PRX_VLOG(n) XLOG(PRX_LOGGING_DBG_##n)
#define PRX_VLOG_IF(n, cond) XLOG_IF(PRX_LOGGING_DBG_##n, (cond))
#define PRX_VLOG_IS_ON(n) XLOG_IS_ON(PRX_LOGGING_DBG_##n)
#define PRX_DLOG(level) XLOG(PRX_LOGLEVEL_##level)
#define PRX_DVLOG(n) XLOG(PRX_LOGGING_DBG_##n)

namespace proxygen::logging::detail {
// PRX_VLOG_LEVEL(n) runtime path: convert int -> folly::LogLevel. n in 0..9
// maps to DBG0..DBG9 (more verbose as n grows). Values >9 saturate at DBG9.
inline constexpr ::folly::LogLevel prxVLogLevel(int n) {
  return static_cast<::folly::LogLevel>(
      static_cast<::std::uint32_t>(::folly::LogLevel::DBG0) -
      static_cast<::std::uint32_t>(n < 0 ? 0 : (n > 9 ? 9 : n)));
}
} // namespace proxygen::logging::detail

// Runtime-level variants for callers that need to pass a non-literal n.
// Uses FB_LOG_RAW with a per-call-site cached Logger so the category lookup
// happens once per site, not once per call. Prefer PRX_VLOG(n) when n is a
// literal — that path stays on the fast XLOG static-cached check.
#define PRX_VLOG_LEVEL(n)                                                    \
  FB_LOG_RAW(([]() -> ::folly::Logger& {                                     \
               static ::folly::Logger _prx_logger{XLOG_GET_CATEGORY_NAME()}; \
               return _prx_logger;                                           \
             }()),                                                           \
             ::proxygen::logging::detail::prxVLogLevel(n),                   \
             XLOG_FILENAME,                                                  \
             __LINE__,                                                       \
             __func__)
#define PRX_DVLOG_LEVEL(n) PRX_VLOG_LEVEL(n)

#define PRX_LOG(level) XLOG(PRX_LOGLEVEL_##level)
#define PRX_LOG_IF(level, cond) XLOG_IF(PRX_LOGLEVEL_##level, (cond))
#define PRX_LOG_EVERY_MS(level, ms) XLOG_EVERY_MS(PRX_LOGLEVEL_##level, ms)
#define PRX_LOG_EVERY_N(level, n) XLOG_EVERY_N(PRX_LOGLEVEL_##level, n)

#define PRX_CHECK XCHECK
#define PRX_CHECK_EQ XCHECK_EQ
#define PRX_CHECK_NE XCHECK_NE
#define PRX_CHECK_GE XCHECK_GE
#define PRX_CHECK_GT XCHECK_GT
#define PRX_CHECK_LE XCHECK_LE
#define PRX_CHECK_LT XCHECK_LT
// XCHECK_NOTNULL is not provided by folly; emulate via a generic lambda that
// asserts non-null and returns the original (possibly-rvalue) pointer so call
// sites like `auto p = PRX_CHECK_NOTNULL(expr);` keep working.
#define PRX_CHECK_NOTNULL(p)                       \
  ([](auto&& _prx_p) -> decltype(auto) {           \
    XCHECK(_prx_p != nullptr);                     \
    return std::forward<decltype(_prx_p)>(_prx_p); \
  }(p))
// XCHECK does not append : <strerror(errno)> the way glog's PCHECK does.
// TODO: replace with XPCHECK once folly provides one.
#define PRX_PCHECK XCHECK
#define PRX_DCHECK XDCHECK
#define PRX_DCHECK_EQ XDCHECK_EQ
#define PRX_DCHECK_NE XDCHECK_NE
#define PRX_DCHECK_GE XDCHECK_GE
#define PRX_DCHECK_GT XDCHECK_GT
#define PRX_DCHECK_LE XDCHECK_LE
#define PRX_DCHECK_LT XDCHECK_LT

#else // PRX_LOGGING_DISABLED

// Proxygen logging disabled
//
// PRX_DCHECK()s are mapped to assert(...)
// PRX_CHECK()s and PRX_PCHECK()s are mapped to if (!(expr)) { abort(); }
//   (PCHECK loses the strerror(errno) suffix glog provides; see TODO above.)
//
// All log output is silently dropped.

#include <cassert>
#include <cstdlib>
#include <utility>

namespace proxygen::logging::detail {
struct NoopStream {};

template <class T>
inline NoopStream operator<<(NoopStream stream, T&&) {
  return stream;
}

} // namespace proxygen::logging::detail

// The value of this is not important. These definitions are here to make sure
// that the loglevel of PRX_LOG() is either {INFO, WARNING, ERROR, FATAL,
// DFATAL} tokens since anything else will cause the comma operator expansion
// to fail.
#define PRX_LOGLEVEL_INFO 0
#define PRX_LOGLEVEL_WARNING 0
#define PRX_LOGLEVEL_ERROR 0
#define PRX_LOGLEVEL_FATAL 0
#define PRX_LOGLEVEL_DFATAL 0

#define PRX_DCHECK_BINOP_(a, op, b)                   \
  ([&] {                                              \
    assert((a)op(b));                                 \
    return ::proxygen::logging::detail::NoopStream{}; \
  }())
#define PRX_CHECK_BINOP_(a, op, b)                    \
  ([&] {                                              \
    if (!((a)op(b))) {                                \
      std::abort();                                   \
    }                                                 \
    return ::proxygen::logging::detail::NoopStream{}; \
  }())
#define PRX_VLOG(level) \
  ([](int) { return ::proxygen::logging::detail::NoopStream{}; }((level)))
#define PRX_VLOG_LEVEL(level) PRX_VLOG(level)
#define PRX_DVLOG_LEVEL(level) PRX_VLOG(level)
#define PRX_VLOG_IF(level, cond)                      \
  ([](int, bool) {                                    \
    return ::proxygen::logging::detail::NoopStream{}; \
  }((level), static_cast<bool>(cond)))
#define PRX_VLOG_IS_ON(level) ([](int) { return false; }((level)))
#define PRX_DLOG(level) PRX_LOG(level)
#define PRX_DVLOG(level) PRX_VLOG(level)
#define PRX_LOG(level) \
  ((void)PRX_LOGLEVEL_##level, ::proxygen::logging::detail::NoopStream{})
#define PRX_LOG_IF(level, cond)   \
  ((void)PRX_LOGLEVEL_##level,    \
   (void)static_cast<bool>(cond), \
   ::proxygen::logging::detail::NoopStream{})
#define PRX_LOG_EVERY_MS(level, ms) \
  ((void)PRX_LOGLEVEL_##level,      \
   (void)(ms),                      \
   ::proxygen::logging::detail::NoopStream{})
#define PRX_LOG_EVERY_N(level, n) \
  ((void)PRX_LOGLEVEL_##level,    \
   (void)(n),                     \
   ::proxygen::logging::detail::NoopStream{})
#define PRX_CHECK(expr)                               \
  ([&] {                                              \
    if (!(expr)) {                                    \
      std::abort();                                   \
    }                                                 \
    return ::proxygen::logging::detail::NoopStream{}; \
  }())
#define PRX_CHECK_EQ(a, b) PRX_CHECK_BINOP_(a, ==, b)
#define PRX_CHECK_NE(a, b) PRX_CHECK_BINOP_(a, !=, b)
#define PRX_CHECK_GE(a, b) PRX_CHECK_BINOP_(a, >=, b)
#define PRX_CHECK_GT(a, b) PRX_CHECK_BINOP_(a, >, b)
#define PRX_CHECK_LE(a, b) PRX_CHECK_BINOP_(a, <=, b)
#define PRX_CHECK_LT(a, b) PRX_CHECK_BINOP_(a, <, b)
#define PRX_CHECK_NOTNULL(p)                       \
  ([](auto&& _prx_p) -> decltype(auto) {           \
    if (_prx_p == nullptr) {                       \
      std::abort();                                \
    }                                              \
    return std::forward<decltype(_prx_p)>(_prx_p); \
  }(p))
// PCHECK loses the strerror(errno) suffix glog provides; see TODO above.
#define PRX_PCHECK PRX_CHECK
#define PRX_DCHECK(expr)                              \
  ([&] {                                              \
    assert((expr));                                   \
    return ::proxygen::logging::detail::NoopStream{}; \
  }())
#define PRX_DCHECK_EQ(a, b) PRX_DCHECK_BINOP_(a, ==, b)
#define PRX_DCHECK_NE(a, b) PRX_DCHECK_BINOP_(a, !=, b)
#define PRX_DCHECK_GE(a, b) PRX_DCHECK_BINOP_(a, >=, b)
#define PRX_DCHECK_GT(a, b) PRX_DCHECK_BINOP_(a, >, b)
#define PRX_DCHECK_LE(a, b) PRX_DCHECK_BINOP_(a, <=, b)
#define PRX_DCHECK_LT(a, b) PRX_DCHECK_BINOP_(a, <, b)

#endif
