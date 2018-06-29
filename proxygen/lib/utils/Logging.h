/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/Optional.h>
#include <folly/io/IOBuf.h>
#include <sstream>
#include <string>

namespace proxygen {

namespace logging_details {
  std::string getStackTrace();
}

class NullStream final : public std::ostream {
 public:
  NullStream() = default;
  NullStream(const NullStream&) = default;
  NullStream& operator=(NullStream&&) {
    return *this;
  }
};

template <typename T>
class StackTracePrinterWithException {
 private:
  using StringStreamPair = std::pair<std::string, std::ostringstream>;
 public:
  StackTracePrinterWithException(
      const bool checkPassed,
      const char* checkString,
      const char* file,
      const int line,
      const int logLevel)
    : file_(file),
      line_(line),
      logLevel_(logLevel) {
    if (checkPassed) {
      nullStream_.emplace();
    } else {
      traceAndLogStreamPair_ = std::make_unique<StringStreamPair>();
      traceAndLogStreamPair_->first = logging_details::getStackTrace();
      traceAndLogStreamPair_->second << checkString;
    }
  }
  std::ostream& stream() {
    if (nullStream_) {
      return nullStream_.value();
    }
    return traceAndLogStreamPair_->second;
  }
  ~StackTracePrinterWithException() noexcept(false) {
    if (!nullStream_) {
      google::LogMessage(file_, line_, logLevel_).stream()
        << traceAndLogStreamPair_->second.str()
        << "\nCall stack:\n"
        << traceAndLogStreamPair_->first;
      throw T(traceAndLogStreamPair_->second.str());
    }
  }
 private:
  const char* file_;
  int line_;
  int logLevel_;
  std::unique_ptr<StringStreamPair> traceAndLogStreamPair_;
  folly::Optional<NullStream> nullStream_;
};

template<class T>
inline NullStream& operator<<(NullStream &ns, const T & /* ignored */) {
  return ns;
}

#define CHECK_LOG_AND_THROW(CONDITION, LOG_LEVEL, EXCEPTION)  \
    (StackTracePrinterWithException<EXCEPTION>(               \
      (CONDITION),                                            \
      "Check failed \"" #CONDITION "\": ",                    \
      __FILE__,                                               \
      __LINE__,                                               \
      google::GLOG_##LOG_LEVEL)).stream()

#define CHECK_LOG_AND_THROW_LT(X, Y, LOG_LEVEL, EXCEPTION)    \
  CHECK_LOG_AND_THROW((X) < (Y), LOG_LEVEL, EXCEPTION)

#define CHECK_LOG_AND_THROW_LE(X, Y, LOG_LEVEL, EXCEPTION)    \
  CHECK_LOG_AND_THROW((X) <= (Y), LOG_LEVEL, EXCEPTION)

#define CHECK_LOG_AND_THROW_GT(X, Y, LOG_LEVEL, EXCEPTION)    \
  CHECK_LOG_AND_THROW((X) > (Y), LOG_LEVEL, EXCEPTION)

#define CHECK_LOG_AND_THROW_GE(X, Y, LOG_LEVEL, EXCEPTION)    \
  CHECK_LOG_AND_THROW((X) >= (Y), LOG_LEVEL, EXCEPTION)

#define CHECK_LOG_AND_THROW_EQ(X, Y, LOG_LEVEL, EXCEPTION)    \
  CHECK_LOG_AND_THROW((X) == (Y), LOG_LEVEL, EXCEPTION)

#define CHECK_LOG_AND_THROW_NE(X, Y, LOG_LEVEL, EXCEPTION)    \
  CHECK_LOG_AND_THROW((X) != (Y), LOG_LEVEL, EXCEPTION)

#define CHECK_LOG_AND_THROW_NOT_NULL(X, LOG_LEVEL, EXCEPTION) \
  CHECK_LOG_AND_THROW((X) != nullptr, LOG_LEVEL, EXCEPTION)

#define CHECK_LOG_AND_THROW_NULL(X, LOG_LEVEL, EXCEPTION)     \
  CHECK_LOG_AND_THROW((X) == nullptr, LOG_LEVEL, EXCEPTION)

class IOBufPrinter {
 public:
  enum class Format : uint8_t {
    HEX_FOLLY = 0,
    HEX_16 = 1,
    CHAIN_INFO = 2,
    BIN = 3,
  };

  static std::string printChain(const folly::IOBuf* buf,
                                Format format,
                                bool coalesce);

  static std::string printHexFolly(const folly::IOBuf* buf,
                                   bool coalesce=false) {
    return printChain(buf, Format::HEX_FOLLY, coalesce);
  }

  static std::string printHex16(const folly::IOBuf* buf, bool coalesce=false) {
    return printChain(buf, Format::HEX_16, coalesce);
  }

  static std::string printChainInfo(const folly::IOBuf* buf) {
    return printChain(buf, Format::CHAIN_INFO, false);
  }

  static std::string printBin(const folly::IOBuf* buf, bool coalesce=false) {
    return printChain(buf, Format::BIN, coalesce);
  }

  IOBufPrinter() {}
  virtual ~IOBufPrinter() {}

  virtual std::string print(const folly::IOBuf* buf) = 0;
};

class Hex16Printer : public IOBufPrinter {
 public:
  std::string print(const folly::IOBuf* buf) override;
};

class HexFollyPrinter : public IOBufPrinter {
 public:
  std::string print(const folly::IOBuf* buf) override;
};

class ChainInfoPrinter : public IOBufPrinter {
 public:
  std::string print(const folly::IOBuf* buf) override;
};

class BinPrinter : public IOBufPrinter {
 public:
  std::string print(const folly::IOBuf* buf) override;
};

/**
 * write the entire binary content from all the buffers into a binary file
 */
void dumpBinToFile(const std::string& filename, const folly::IOBuf* buf);

/**
 * helper functions for printing in hex a byte array
 * see unit test for example
 */
std::string hexStr(folly::StringPiece sp);

}
