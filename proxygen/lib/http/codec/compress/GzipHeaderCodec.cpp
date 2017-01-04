/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/GzipHeaderCodec.h>

#include <folly/Memory.h>
#include <folly/SingletonThreadLocal.h>
#include <folly/String.h>
#include <folly/ThreadLocal.h>
#include <folly/io/IOBuf.h>
#include <proxygen/lib/http/codec/SPDYCodec.h>
#include <proxygen/lib/http/codec/SPDYConstants.h>
#include <proxygen/lib/utils/Logging.h>
#include <string>

using folly::IOBuf;
using folly::ThreadLocalPtr;
using folly::io::Cursor;
using namespace proxygen;
using proxygen::compress::Header;
using proxygen::compress::HeaderPiece;
using proxygen::compress::HeaderPieceList;
using proxygen::spdy::kMaxFrameLength;
using std::string;
using std::unique_ptr;
using std::vector;

namespace {

// Maximum size of header names+values after expanding multi-value headers
const size_t kMaxExpandedHeaderLineBytes = 80 * 1024;

// Pre-initialized compression contexts seeded with the
// starting dictionary for different SPDY versions - cloning
// one of these is faster than initializing and seeding a
// brand new deflate context.
struct ZlibConfig {

  ZlibConfig(SPDYVersion inVersion, int inCompressionLevel)
      : version(inVersion), compressionLevel(inCompressionLevel) {}

  bool operator==(const ZlibConfig& lhs) const {
    return (version == lhs.version) &&
      (compressionLevel == lhs.compressionLevel);
  }

  bool operator<(const ZlibConfig& lhs) const {
    return (version < lhs.version) ||
        ((version == lhs.version) &&
         (compressionLevel < lhs.compressionLevel));
  }
  SPDYVersion version;
  int compressionLevel;
};

struct ZlibContext {
  ~ZlibContext() {
    deflateEnd(&deflater);
    inflateEnd(&inflater);
  }

  z_stream deflater;
  z_stream inflater;
};

namespace { struct BufferTag {}; }
static folly::SingletonThreadLocal<unique_ptr<IOBuf>, BufferTag> s_buf{};
folly::IOBuf& getStaticHeaderBufSpace(size_t size) {
  if (!s_buf.get()) {
    s_buf.get() = folly::make_unique<IOBuf>(IOBuf::CREATE, size);
  } else {
    if (size > s_buf.get()->capacity()) {
      s_buf.get() = folly::make_unique<IOBuf>(IOBuf::CREATE, size);
    } else {
      s_buf.get()->clear();
    }
  }
  DCHECK(!s_buf.get()->isShared());
  return *s_buf.get();
}

void appendString(uint8_t*& dst, const string& str) {
  size_t len = str.length();
  memcpy(dst, str.data(), len);
  dst += len;
}

using ZlibContextMap = std::map<ZlibConfig, std::unique_ptr<ZlibContext>>;
namespace { struct ContextTag {}; }
static folly::SingletonThreadLocal<ZlibContextMap, ContextTag> s_zlibContexts{};
/**
 * get the thread local cached zlib context
 */
static const ZlibContext* getZlibContext(SPDYVersionSettings versionSettings,
                                         int compressionLevel) {
  ZlibConfig zlibConfig(versionSettings.version, compressionLevel);
  auto match = s_zlibContexts.get().find(zlibConfig);
  if (match != s_zlibContexts.get().end()) {
    return match->second.get();
  } else {
    // This is the first request for the specified SPDY version and compression
    // level in this thread, so we need to construct the initial compressor and
    // decompressor contexts.
    auto newContext = folly::make_unique<ZlibContext>();
    newContext->deflater.zalloc = Z_NULL;
    newContext->deflater.zfree = Z_NULL;
    newContext->deflater.opaque = Z_NULL;
    newContext->deflater.avail_in = 0;
    newContext->deflater.next_in = Z_NULL;
    int windowBits  = (compressionLevel == Z_NO_COMPRESSION) ? 8 : 11;
    int r = deflateInit2(
        &(newContext->deflater),
        compressionLevel,
        Z_DEFLATED, // compression method
        windowBits, // log2 of the compression window size, negative value
                    // means raw deflate output format w/o libz header
        1,          // memory size for internal compression state, 1-9
        Z_DEFAULT_STRATEGY);
    CHECK_EQ(r, Z_OK);
    if (compressionLevel != Z_NO_COMPRESSION) {
      r = deflateSetDictionary(&(newContext->deflater), versionSettings.dict,
                               versionSettings.dictSize);
      CHECK_EQ(r, Z_OK);
    }

    newContext->inflater.zalloc = Z_NULL;
    newContext->inflater.zfree = Z_NULL;
    newContext->inflater.opaque = Z_NULL;
    newContext->inflater.avail_in = 0;
    newContext->inflater.next_in = Z_NULL;
// TODO (t6405700): Port the zlib optimization forward to 1.2.8 for gcc 4.9
#if ZLIB_VERNUM == 0x1250
    // set zlib's reserved flag to allocate smaller initial sliding window, then
    // double it if necessary
    newContext->inflater.reserved = 0x01;
#endif
    r = inflateInit2(&(newContext->inflater), 0);
    CHECK_EQ(r, Z_OK);

    auto result = newContext.get();
    s_zlibContexts.get().emplace(zlibConfig, std::move(newContext));
    return result;
  }
}

} // anonymous namespace

namespace proxygen {

GzipHeaderCodec::GzipHeaderCodec(int compressionLevel,
                                 const SPDYVersionSettings& versionSettings)
    : versionSettings_(versionSettings) {
  // Create compression and decompression contexts by cloning thread-local
  // copies of the initial SPDY compression state
  auto context = getZlibContext(versionSettings, compressionLevel);
  deflateCopy(&deflater_, const_cast<z_stream*>(&(context->deflater)));
  inflateCopy(&inflater_, const_cast<z_stream*>(&(context->inflater)));
}

GzipHeaderCodec::GzipHeaderCodec(int compressionLevel,
                                 SPDYVersion version)
    : GzipHeaderCodec(
        compressionLevel,
        SPDYCodec::getVersionSettings(version)) {}

GzipHeaderCodec::~GzipHeaderCodec() {
  deflateEnd(&deflater_);
  inflateEnd(&inflater_);
}

folly::IOBuf& GzipHeaderCodec::getHeaderBuf() {
  return getStaticHeaderBufSpace(maxUncompressed_);
}

unique_ptr<IOBuf> GzipHeaderCodec::encode(vector<Header>& headers) noexcept {
  // Build a sequence of the header names and values, sorted by name.
  // The purpose of the sort is to make it easier to combine the
  // values of multiple headers with the same name.  The SPDY spec
  // prohibits any header name from appearing more than once in the
  // Name/Value list, so we must combine values when serializing.
  std::sort(headers.begin(), headers.end());

  auto& uncompressed = getHeaderBuf();
  // Compute the amount of space needed to hold the uncompressed
  // representation of the headers.  This is an upper bound on the
  // amount of space we'll actually need, because if we end up
  // combining any headers with the same name, the combined
  // representation will be smaller than the original.
  size_t maxUncompressedSize = versionSettings_.nameValueSize;
  for (const Header& header : headers) {
    maxUncompressedSize += versionSettings_.nameValueSize;
    maxUncompressedSize += header.name->length();
    maxUncompressedSize += versionSettings_.nameValueSize;
    maxUncompressedSize += header.value->length();
  }

  // TODO: give on 'onError()' callback if the space in uncompressed buf
  // cannot fit the headers and then skip the "reserve" code below. We
  // have already reserved the maximum legal amount of space for
  // uncompressed headers.

  VLOG(5) << "reserving " << maxUncompressedSize
          << " bytes for uncompressed headers";
  uncompressed.reserve(0, maxUncompressedSize);

  // Serialize the uncompressed representation of the headers.
  uint8_t* dst = uncompressed.writableData();
  dst += versionSettings_.nameValueSize; // Leave space for count of headers.
  HTTPHeaderCode lastCode = HTTP_HEADER_OTHER;
  const string* lastName = &empty_string;
  uint8_t* lastValueLenPtr = nullptr;
  size_t lastValueLen = 0;
  unsigned numHeaders = 0;
  for (const Header& header : headers) {
    if ((header.code != lastCode) || (*header.name != *lastName)) {
      // Simple case: this header name is different from the previous
      // one, so we don't need to combine values.
      numHeaders++;
      versionSettings_.appendSizeFun(dst, header.name->length());

      // lowercasing the header name inline
      char* nameBegin = (char *)dst;
      appendString(dst, *header.name);
      folly::toLowerAscii((char *)nameBegin, header.name->size());

      lastValueLenPtr = dst;
      lastValueLen = header.value->length();
      versionSettings_.appendSizeFun(dst, header.value->length());
      appendString(dst, *header.value);
      lastCode = header.code;
      lastName = header.name;
    } else if (header.value->length() > 0) {
      // More complicated case: we do need to combine values.
      if (lastValueLen > 0) {
        // Only nul terminate if previous value was non-empty
        *dst++ = 0;  // SPDY uses a null byte as a separator
        lastValueLen++;
      }
      appendString(dst, *header.value);
      // Go back and rewrite the length field in front of the value
      lastValueLen += header.value->length();
      uint8_t* tmp = lastValueLenPtr;
      versionSettings_.appendSizeFun(tmp, lastValueLen);
    }
  }

  // Compute the uncompressed length; if we combined any header values,
  // we will have used less space than originally estimated.
  size_t uncompressedLen = dst - uncompressed.writableData();

  // Go back and write the count of unique header names at the start.
  dst = uncompressed.writableData();
  versionSettings_.appendSizeFun(dst, numHeaders);

  // Allocate a contiguous space big enough to hold the compressed headers,
  // plus any headroom requested by the caller.
  size_t maxDeflatedSize = deflateBound(&deflater_, uncompressedLen);
  unique_ptr<IOBuf> out(IOBuf::create(maxDeflatedSize + encodeHeadroom_));
  out->advance(encodeHeadroom_);

  // Compress
  deflater_.next_in = uncompressed.writableData();
  deflater_.avail_in = uncompressedLen;
  deflater_.next_out = out->writableData();
  deflater_.avail_out = maxDeflatedSize;
  int r = deflate(&deflater_, Z_SYNC_FLUSH);
  CHECK_EQ(r, Z_OK);
  CHECK_EQ(deflater_.avail_in, 0);
  out->append(maxDeflatedSize - deflater_.avail_out);

  VLOG(4) << "header size orig=" << uncompressedLen
          << ", max deflated=" << maxDeflatedSize
          << ", actual deflated=" << out->length();

  encodedSize_.compressed = out->length();
  encodedSize_.uncompressed = uncompressedLen;
  if (stats_) {
    stats_->recordEncode(Type::GZIP, encodedSize_);
  }

  return out;
}

Result<HeaderDecodeResult, HeaderDecodeError>
GzipHeaderCodec::decode(Cursor& cursor, uint32_t length) noexcept {
  outHeaders_.clear();

  // empty header block
  if (length == 0) {
    return HeaderDecodeResult{outHeaders_, 0};
  }

  // Get the thread local buffer space to use
  auto& uncompressed = getHeaderBuf();
  uint32_t consumed = 0;
  // Decompress the headers
  while (length > 0) {
    auto next = cursor.peek();
    uint32_t chunkLen = std::min((uint32_t)next.second, length);
    inflater_.avail_in = chunkLen;
    inflater_.next_in = (uint8_t *)next.first;
    do {
      if (uncompressed.tailroom() == 0) {
        // This code should not execute, since we throw an error if the
        // decompressed size of the headers is too large and we initialize
        // the buffer to that size.
        LOG(ERROR) << "Doubling capacity of SPDY headers buffer";
        uncompressed.reserve(0, uncompressed.capacity());
      }

      inflater_.next_out = uncompressed.writableTail();
      inflater_.avail_out = uncompressed.tailroom();
      int r = inflate(&inflater_, Z_NO_FLUSH);
      if (r == Z_NEED_DICT) {
        // we cannot initialize the inflater dictionary before calling inflate()
        // as it checks the adler-32 checksum of the supplied dictionary
        r = inflateSetDictionary(&inflater_, versionSettings_.dict,
                                 versionSettings_.dictSize);
        if (r != Z_OK) {
          LOG(ERROR) << "inflate set dictionary failed with error=" << r;
          return HeaderDecodeError::INFLATE_DICTIONARY;
        }
        inflater_.avail_out = 0;
        continue;
      }
      if (r != 0) {
        // probably bad encoding
        LOG(ERROR) << "inflate failed with error=" << r;
        return HeaderDecodeError::BAD_ENCODING;
      }
      uncompressed.append(uncompressed.tailroom() - inflater_.avail_out);
      if (uncompressed.length() > maxUncompressed_) {
        LOG(ERROR) << "Decompressed headers too large";
        return HeaderDecodeError::HEADERS_TOO_LARGE;
      }
    } while (inflater_.avail_in > 0 && inflater_.avail_out == 0);
    length -= chunkLen;
    consumed += chunkLen;
    cursor.skip(chunkLen);
  }

  decodedSize_.compressed = consumed;
  decodedSize_.uncompressed = uncompressed.computeChainDataLength();
  if (stats_) {
    stats_->recordDecode(Type::GZIP, decodedSize_);
  }

  size_t expandedHeaderLineBytes = 0;
  auto result = parseNameValues(uncompressed, decodedSize_.uncompressed);
  if (result.isError()) {
    return result.error();
  }
  expandedHeaderLineBytes = result.ok();

  if (UNLIKELY(expandedHeaderLineBytes > kMaxExpandedHeaderLineBytes)) {
    LOG(ERROR) << "expanded headers too large";
    return HeaderDecodeError::HEADERS_TOO_LARGE;
  }

  return HeaderDecodeResult{outHeaders_, consumed};
}

void GzipHeaderCodec::decodeStreaming(
    Cursor& cursor,
    uint32_t length,
    HeaderCodec::StreamingCallback* streamingCb) noexcept {
  // TODO: to implement, never called
}

Result<size_t, HeaderDecodeError>
GzipHeaderCodec::parseNameValues(const folly::IOBuf& uncompressed,
                                 uint32_t uncompressedLength) noexcept {

  size_t expandedHeaderLineBytes = 0;
  Cursor headerCursor(&uncompressed);
  uint32_t numNV = 0;
  const HeaderPiece* headerName = nullptr;

  try {
    numNV = versionSettings_.parseSizeFun(&headerCursor);
  } catch (const std::out_of_range& ex) {
    return HeaderDecodeError::BAD_ENCODING;
  }

  for (uint32_t i = 0; i < numNV * 2; i++) {
    uint32_t len = 0;
    try {
      len = versionSettings_.parseSizeFun(&headerCursor);
      uncompressedLength -= versionSettings_.nameValueSize;
    } catch (const std::out_of_range& ex) {
      return HeaderDecodeError::BAD_ENCODING;
    }

    if (len == 0 && !headerName) {
      LOG(ERROR) << "empty header name";
      return HeaderDecodeError::EMPTY_HEADER_NAME;
    }
    auto next = headerCursor.peek();
    try {
      if (len > uncompressedLength) {
        throw std::out_of_range(
          folly::to<string>("bad length=", len, " uncompressedLength=",
                            uncompressedLength));
      } else if (next.second >= len) {
        // string is contiguous, just put a pointer into the headers structure
        outHeaders_.emplace_back((char *)next.first, len, false, false);
        headerCursor.skip(len);
      } else {
        // string is not contiguous, allocate a buffer and pull into it
        unique_ptr<char[]> data (new char[len]);
        headerCursor.pull(data.get(), len);
        outHeaders_.emplace_back(data.release(), len, true, false);
      }
      uncompressedLength -= len;
    } catch (const std::out_of_range& ex) {
      LOG(ERROR) << "bad encoding for nv=" << i << ": "
                 << folly::exceptionStr(ex);
      LOG(ERROR) << IOBufPrinter::printHexFolly(&uncompressed, true);
      return HeaderDecodeError::BAD_ENCODING;
    }
    if (i % 2 == 0) {
      headerName = &outHeaders_.back();
      for (const char c: headerName->str) {
        if (c < 0x20 || c > 0x7e || ('A' <= c && c <= 'Z')) {
          LOG(ERROR) << "invalid header value";
          return HeaderDecodeError::INVALID_HEADER_VALUE;
        }
      }
    } else {
      HeaderPiece& headerValue = outHeaders_.back();
      bool first = true;
      const char* valueStart = headerValue.str.data();
      const char* pos = valueStart;
      const char* stop = valueStart + headerValue.str.size();
      while(pos < stop) {
        if (*pos == '\0') {
          if (pos - valueStart == 0) {
            LOG(ERROR) << "empty header value for header=" << headerName;
            return HeaderDecodeError::EMPTY_HEADER_VALUE;
          }
          if (first) {
            headerValue.str.reset(valueStart, pos - valueStart);
            first = false;
          } else {
            outHeaders_.emplace_back(headerName->str.data(),
                                     headerName->str.size(),
                                     false, true);
            outHeaders_.emplace_back(valueStart, pos - valueStart, false, true);
            expandedHeaderLineBytes += ((pos - valueStart) +
                                        headerName->str.size());
          }
          valueStart = pos + 1;
        }
        pos++;
      }
      if (!first) {
        // value contained at least one \0, add the last value
        if (pos - valueStart == 0) {
          LOG(ERROR) << "empty header value for header=" << headerName;
          return HeaderDecodeError::EMPTY_HEADER_VALUE;
        }
        outHeaders_.emplace_back(headerName->str.data(),
                                 headerName->str.size(),
                                 false, true);
        outHeaders_.emplace_back(valueStart, pos - valueStart, false, true);
        expandedHeaderLineBytes += (pos - valueStart) + headerName->str.size();
      }
      headerName = nullptr;
    }
  }
  return expandedHeaderLineBytes;
}

}
