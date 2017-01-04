/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/HTTP2Framer.h>

using namespace folly::io;
using namespace folly;

namespace proxygen { namespace http2 {

const uint8_t kMaxFrameType = static_cast<uint8_t>(FrameType::ALTSVC);
const boost::optional<uint8_t> kNoPadding;
const PriorityUpdate DefaultPriority{0, false, 15};

namespace {

const uint32_t kLengthMask = 0x00ffffff;
const uint32_t kUint31Mask = 0x7fffffff;

static const uint64_t kZeroPad[32] = {0};

static const bool kStrictPadding = true;

static_assert(sizeof(kZeroPad) == 256, "bad zero padding");

void writePriorityBody(IOBufQueue& queue,
                       uint32_t streamDependency,
                       bool exclusive,
                       uint8_t weight) {
  DCHECK_EQ(0, ~kUint31Mask & streamDependency);

  if (exclusive) {
    streamDependency |= ~kUint31Mask;
  }

  QueueAppender appender(&queue, 8);
  appender.writeBE<uint32_t>(streamDependency);
  appender.writeBE<uint8_t>(weight);
}

void writePadding(IOBufQueue& queue, boost::optional<uint8_t> size) {
  if (size && size.get() > 0) {
    auto out = queue.preallocate(size.get(), size.get());
    memset(out.first, 0, size.get());
    queue.postallocate(size.get());
  }
}

/**
 * Generate just the common frame header. This includes the padding length
 * bits that sometimes come right after the frame header. Returns the
 * length field written to the common frame header.
 */
size_t writeFrameHeader(IOBufQueue& queue,
                        uint32_t length,
                        FrameType type,
                        uint8_t flags,
                        uint32_t stream,
                        boost::optional<uint8_t> padding,
                        boost::optional<PriorityUpdate> priority,
                        std::unique_ptr<IOBuf> payload) noexcept {
  size_t headerSize = kFrameHeaderSize;

  // the acceptable length is now conditional based on state :(
  DCHECK_EQ(0, ~kLengthMask & length);
  DCHECK_EQ(0, ~kUint31Mask & stream);

  // Adjust length if we will emit a priority section
  if (flags & PRIORITY) {
    DCHECK(FrameType::HEADERS == type);
    length += kFramePrioritySize;
    headerSize += kFramePrioritySize;
    DCHECK_EQ(0, ~kLengthMask & length);
  }

  // Add or remove padding flags
  if (padding) {
    flags |= PADDED;
    length += padding.get() + 1;
    headerSize += 1;
  } else {
    flags &= ~PADDED;
  }

  if (priority) {
    headerSize += kFramePrioritySize;
  }

  DCHECK_EQ(0, ~kLengthMask & length);
  DCHECK_GE(kMaxFrameType, static_cast<uint8_t>(type));
  uint32_t lengthAndType =
    ((kLengthMask & length) << 8) | static_cast<uint8_t>(type);

  uint64_t payloadLength = 0;
  if (payload && !payload->isSharedOne() &&
      payload->headroom() >= headerSize &&
      queue.tailroom() < headerSize) {
    // Use the headroom in payload for the frame header.
    // Make it appear that the payload IOBuf is empty and retreat so
    // appender can access the headroom
    payloadLength = payload->length();
    payload->trimEnd(payloadLength);
    payload->retreat(headerSize);
    auto tail = payload->pop();
    queue.append(std::move(payload));
    payload = std::move(tail);
  }
  QueueAppender appender(&queue, kFrameHeaderSize);
  appender.writeBE<uint32_t>(lengthAndType);
  appender.writeBE<uint8_t>(flags);
  appender.writeBE<uint32_t>(kUint31Mask & stream);

  if (padding) {
    appender.writeBE<uint8_t>(padding.get());
  }
  if (priority) {
    DCHECK_NE(priority->streamDependency, stream) << "Circular dependecy";
    writePriorityBody(queue,
                      priority->streamDependency,
                      priority->exclusive,
                      priority->weight);
  }
  if (payloadLength) {
    queue.postallocate(payloadLength);
  }
  queue.append(std::move(payload));

  return length;
}

uint32_t parseUint31(Cursor& cursor) {
  // MUST ignore the 1 bit before the stream-id
  return kUint31Mask & cursor.readBE<uint32_t>();
}

ErrorCode parseErrorCode(Cursor& cursor, ErrorCode& outCode) {
  auto code = cursor.readBE<uint32_t>();
  if (code > kMaxErrorCode) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  outCode = ErrorCode(code);
  return ErrorCode::NO_ERROR;
}

PriorityUpdate parsePriorityCommon(Cursor& cursor) {
  PriorityUpdate priority;
  uint32_t streamAndExclusive = cursor.readBE<uint32_t>();
  priority.weight = cursor.readBE<uint8_t>();
  priority.exclusive = ~kUint31Mask & streamAndExclusive;
  priority.streamDependency = kUint31Mask & streamAndExclusive;
  return priority;
}

/**
 * Given the flags for a frame and the cursor pointing at the top of the
 * frame-specific section (after the common header), return the number of
 * bytes to skip at the end of the frame. Caller must ensure there is at
 * least 1 bytes in the cursor.
 *
 * @param cursor The cursor to pull data from
 * @param header The frame header for the frame being parsed. The length
 *               field may be modified based on the number of optional
 *               padding length fields were read.
 * @param padding The out parameter that will return the number of padding
 *                bytes at the end of the frame.
 * @return Nothing if success. The connection error code if failure.
 */
ErrorCode
parsePadding(Cursor& cursor,
             FrameHeader& header,
             uint8_t& padding) noexcept {
  if (frameHasPadding(header)) {
    if (header.length < 1) {
      return ErrorCode::FRAME_SIZE_ERROR;
    }
    header.length -= 1;
    padding = cursor.readBE<uint8_t>();
  } else {
    padding = 0;
  }
  return ErrorCode::NO_ERROR;
}

ErrorCode
skipPadding(Cursor& cursor,
            uint8_t length,
            bool verify) {
  if (verify) {
    while (length > 0) {
      auto cur = cursor.peek();
      uint8_t toCmp = std::min<size_t>(cur.second, length);
      if (memcmp(cur.first, kZeroPad, toCmp)) {
        return ErrorCode::PROTOCOL_ERROR;
      }
      cursor.skip(toCmp);
      length -= toCmp;
    }
  } else {
    cursor.skip(length);
  }
  return ErrorCode::NO_ERROR;
}

} // anonymous namespace

bool frameAffectsCompression(FrameType t) {
  return t == FrameType::HEADERS ||
    t == FrameType::PUSH_PROMISE ||
    t == FrameType::CONTINUATION;
}

bool frameHasPadding(const FrameHeader& header) {
  return header.flags & PADDED;
}

//// Parsing ////

ErrorCode
parseFrameHeader(Cursor& cursor,
                 FrameHeader& header) noexcept {
  DCHECK_LE(kFrameHeaderSize, cursor.totalLength());

  // MUST ignore the 2 bits before the length
  uint32_t lengthAndType = cursor.readBE<uint32_t>();
  header.length = kLengthMask & (lengthAndType >> 8);
  uint8_t type = lengthAndType & 0xff;
  header.type = FrameType(type);
  header.flags = cursor.readBE<uint8_t>();
  header.stream = parseUint31(cursor);
  return ErrorCode::NO_ERROR;
}

ErrorCode
parseData(Cursor& cursor,
          FrameHeader header,
          std::unique_ptr<IOBuf>& outBuf,
          uint16_t& outPadding) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  if (header.stream == 0) {
    return ErrorCode::PROTOCOL_ERROR;
  }

  uint8_t padding;
  const auto err = parsePadding(cursor, header, padding);
  RETURN_IF_ERROR(err);
  if (header.length < padding) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  // outPadding is the total number of flow-controlled pad bytes, which
  // includes the length byte, if present.
  outPadding = padding + ((frameHasPadding(header)) ? 1 : 0);
  cursor.clone(outBuf, header.length - padding);
  return skipPadding(cursor, padding, kStrictPadding);
}

ErrorCode
parseDataBegin(Cursor& cursor,
               FrameHeader header,
               size_t& parsed,
               uint16_t& outPadding) noexcept {
  uint8_t padding = 0;
  const auto err = http2::parsePadding(cursor, header, padding);
  RETURN_IF_ERROR(err);
  if (header.length < padding) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  // outPadding is the total number of flow-controlled pad bytes, which
  // includes the length byte, if present.
  outPadding = padding + ((frameHasPadding(header)) ? 1 : 0);
  return ErrorCode::NO_ERROR;
}

ErrorCode
parseDataEnd(Cursor& cursor,
             const size_t bufLen,
             const size_t pendingDataFramePaddingBytes,
             size_t& toSkip) noexcept {
    toSkip = std::min(pendingDataFramePaddingBytes, bufLen);
    return skipPadding(cursor, toSkip, kStrictPadding);
}

ErrorCode
parseHeaders(Cursor& cursor,
             FrameHeader header,
             boost::optional<PriorityUpdate>& outPriority,
             std::unique_ptr<IOBuf>& outBuf) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  if (header.stream == 0) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  uint8_t padding;

  auto err = parsePadding(cursor, header, padding);
  RETURN_IF_ERROR(err);
  if (header.flags & PRIORITY) {
    if (header.length < kFramePrioritySize) {
      return ErrorCode::FRAME_SIZE_ERROR;
    }
    outPriority = parsePriorityCommon(cursor);
    header.length -= kFramePrioritySize;
  } else {
    outPriority = boost::none;
  }
  if (header.length < padding) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  cursor.clone(outBuf, header.length - padding);
  return skipPadding(cursor, padding, kStrictPadding);
}

ErrorCode
parsePriority(Cursor& cursor,
              FrameHeader header,
              PriorityUpdate& outPriority) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  if (header.length != kFramePrioritySize) {
    return ErrorCode::FRAME_SIZE_ERROR;
  }
  if (header.stream == 0) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  outPriority = parsePriorityCommon(cursor);
  return ErrorCode::NO_ERROR;
}

ErrorCode
parseRstStream(Cursor& cursor,
               FrameHeader header,
               ErrorCode& outCode) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  if (header.length != kFrameRstStreamSize) {
    return ErrorCode::FRAME_SIZE_ERROR;
  }
  if (header.stream == 0) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  return parseErrorCode(cursor, outCode);
}

ErrorCode
parseSettings(Cursor& cursor,
              FrameHeader header,
              std::deque<SettingPair>& settings) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  if (header.stream != 0) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  if (header.flags & ACK) {
    if (header.length != 0) {
      return ErrorCode::FRAME_SIZE_ERROR;
    }
    return ErrorCode::NO_ERROR;
  }

  if (header.length % 6 != 0) {
    return ErrorCode::FRAME_SIZE_ERROR;
  }
  for (; header.length > 0; header.length -= 6) {
    uint16_t id = cursor.readBE<uint16_t>();
    uint32_t val = cursor.readBE<uint32_t>();
    settings.push_back(std::make_pair(SettingsId(id), val));
  }
  return ErrorCode::NO_ERROR;
}

ErrorCode
parsePushPromise(Cursor& cursor,
                 FrameHeader header,
                 uint32_t& outPromisedStream,
                 std::unique_ptr<IOBuf>& outBuf) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  if (header.stream == 0) {
    return ErrorCode::PROTOCOL_ERROR;
  }

  uint8_t padding;
  auto err = parsePadding(cursor, header, padding);
  RETURN_IF_ERROR(err);
  if (header.length < kFramePushPromiseSize) {
    return ErrorCode::FRAME_SIZE_ERROR;
  }
  header.length -= kFramePushPromiseSize;
  outPromisedStream = parseUint31(cursor);
  if (outPromisedStream == 0 ||
      outPromisedStream & 0x1) {
    // client MUST reserve an even stream id greater than 0
    return ErrorCode::PROTOCOL_ERROR;
  }
  if (header.length < padding) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  cursor.clone(outBuf, header.length - padding);
  return skipPadding(cursor, padding, kStrictPadding);
}

ErrorCode
parsePing(Cursor& cursor,
          FrameHeader header,
          uint64_t& outOpaqueData) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());

  if (header.length != kFramePingSize) {
    return ErrorCode::FRAME_SIZE_ERROR;
  }
  if (header.stream != 0) {
    return ErrorCode::PROTOCOL_ERROR;
  }

  cursor.pull(&outOpaqueData, sizeof(outOpaqueData));
  return ErrorCode::NO_ERROR;
}

ErrorCode
parseGoaway(Cursor& cursor,
            FrameHeader header,
            uint32_t& outLastStreamID,
            ErrorCode& outCode,
            std::unique_ptr<IOBuf>& outDebugData) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  if (header.length < kFrameGoawaySize) {
    return ErrorCode::FRAME_SIZE_ERROR;
  }
  if (header.stream != 0) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  outLastStreamID = parseUint31(cursor);
  auto err = parseErrorCode(cursor, outCode);
  RETURN_IF_ERROR(err);
  header.length -= kFrameGoawaySize;
  if (header.length > 0) {
    cursor.clone(outDebugData, header.length);
  }
  return ErrorCode::NO_ERROR;
}

ErrorCode
parseWindowUpdate(Cursor& cursor,
                  FrameHeader header,
                  uint32_t& outAmount) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  if (header.length != kFrameWindowUpdateSize) {
    return ErrorCode::FRAME_SIZE_ERROR;
  }
  outAmount = parseUint31(cursor);
  return ErrorCode::NO_ERROR;
}

ErrorCode
parseContinuation(Cursor& cursor,
                  FrameHeader header,
                  std::unique_ptr<IOBuf>& outBuf) noexcept {
  DCHECK(header.type == FrameType::CONTINUATION);
  DCHECK_LE(header.length, cursor.totalLength());
  if (header.stream == 0) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  uint8_t padding;

  auto err = parsePadding(cursor, header, padding);
  RETURN_IF_ERROR(err);
  if (header.length < padding) {
    return ErrorCode::PROTOCOL_ERROR;
  }
  cursor.clone(outBuf, header.length - padding);
  return skipPadding(cursor, padding, kStrictPadding);
}

ErrorCode
parseAltSvc(Cursor& cursor,
            FrameHeader header,
            uint32_t& outMaxAge,
            uint32_t& outPort,
            std::string& outProtocol,
            std::string& outHost,
            std::string& outOrigin) noexcept {
  DCHECK_LE(header.length, cursor.totalLength());
  if (header.length < kFrameAltSvcSizeBase) {
    return ErrorCode::FRAME_SIZE_ERROR;
  }
  std::unique_ptr<IOBuf> tmpBuf;

  outMaxAge = cursor.readBE<uint32_t>();
  outPort = cursor.readBE<uint16_t>();
  const auto protoLen = cursor.readBE<uint8_t>();
  if (header.length < kFrameAltSvcSizeBase + protoLen) {
    return ErrorCode::FRAME_SIZE_ERROR;
  }
  outProtocol = cursor.readFixedString(protoLen);
  const auto hostLen = cursor.readBE<uint8_t>();
  if (header.length < kFrameAltSvcSizeBase + protoLen + hostLen) {
    return ErrorCode::FRAME_SIZE_ERROR;
  }
  outHost = cursor.readFixedString(hostLen);
  const auto originLen = (header.length - kFrameAltSvcSizeBase -
                          protoLen - hostLen);
  outOrigin = cursor.readFixedString(originLen);

  return ErrorCode::NO_ERROR;
}

//// Egress ////

size_t
writeData(IOBufQueue& queue,
          std::unique_ptr<IOBuf> data,
          uint32_t stream,
          boost::optional<uint8_t> padding,
          bool endStream) noexcept {
  DCHECK_NE(0, stream);
  uint8_t flags = 0;
  if (endStream) {
    flags |= END_STREAM;
  }
  const uint64_t dataLen = data ? data->computeChainDataLength() : 0;
  // Caller must not exceed peer setting for MAX_FRAME_SIZE
  // TODO: look into using headroom from data to hold the frame header
  const auto frameLen = writeFrameHeader(queue,
                                         dataLen,
                                         FrameType::DATA,
                                         flags,
                                         stream,
                                         padding,
                                         boost::none,
                                         std::move(data));
  writePadding(queue, padding);
  return kFrameHeaderSize + frameLen;
}

size_t
writeHeaders(IOBufQueue& queue,
             std::unique_ptr<IOBuf> headers,
             uint32_t stream,
             boost::optional<PriorityUpdate> priority,
             boost::optional<uint8_t> padding,
             bool endStream,
             bool endHeaders) noexcept {
  DCHECK_NE(0, stream);
  const auto dataLen = (headers) ? headers->computeChainDataLength() : 0;
  uint32_t flags = 0;
  if (priority) {
    flags |= PRIORITY;
  }
  if (endStream) {
    flags |= END_STREAM;
  }
  if (endHeaders) {
    flags |= END_HEADERS;
  }
  // padding flags handled directly inside writeFrameHeader()
  const auto frameLen = writeFrameHeader(queue,
                                         dataLen,
                                         FrameType::HEADERS,
                                         flags,
                                         stream,
                                         padding,
                                         priority,
                                         std::move(headers));
  writePadding(queue, padding);
  return kFrameHeaderSize + frameLen;
}

size_t
writePriority(IOBufQueue& queue,
              uint32_t stream,
              PriorityUpdate priority) noexcept {
  DCHECK_NE(0, stream);
  const auto frameLen = writeFrameHeader(queue,
                                         kFramePrioritySize,
                                         FrameType::PRIORITY,
                                         0,
                                         stream,
                                         kNoPadding,
                                         priority,
                                         nullptr);
  return kFrameHeaderSize + frameLen;
}

size_t
writeRstStream(IOBufQueue& queue,
               uint32_t stream,
               ErrorCode errorCode) noexcept {
  DCHECK_NE(0, stream);
  const auto frameLen = writeFrameHeader(queue,
                                         kFrameRstStreamSize,
                                         FrameType::RST_STREAM,
                                         0,
                                         stream,
                                         kNoPadding,
                                         boost::none,
                                         nullptr);
  QueueAppender appender(&queue, frameLen);
  appender.writeBE<uint32_t>(static_cast<uint32_t>(errorCode));
  return kFrameHeaderSize + frameLen;
}

size_t
writeSettings(IOBufQueue& queue,
              const std::deque<SettingPair>& settings) {
  const auto settingsSize = settings.size() * 6;
  const auto frameLen = writeFrameHeader(queue,
                                         settingsSize,
                                         FrameType::SETTINGS,
                                         0,
                                         0,
                                         kNoPadding,
                                         boost::none,
                                         nullptr);
  QueueAppender appender(&queue, settingsSize);
  for (const auto& setting: settings) {
    DCHECK_LE(static_cast<uint32_t>(setting.first),
              std::numeric_limits<uint16_t>::max());
    appender.writeBE<uint16_t>(static_cast<uint16_t>(setting.first));
    appender.writeBE<uint32_t>(setting.second);
  }
  return kFrameHeaderSize + frameLen;
}

size_t
writeSettingsAck(IOBufQueue& queue) {
  writeFrameHeader(queue,
                   0,
                   FrameType::SETTINGS,
                   ACK,
                   0,
                   kNoPadding,
                   boost::none,
                   nullptr);
  return kFrameHeaderSize;
}

size_t
writePushPromise(IOBufQueue& queue,
                 uint32_t associatedStream,
                 uint32_t promisedStream,
                 std::unique_ptr<IOBuf> headers,
                 boost::optional<uint8_t> padding,
                 bool endHeaders) noexcept {
  DCHECK_NE(0, promisedStream);
  DCHECK_NE(0, associatedStream);
  DCHECK_EQ(0, 0x1 & promisedStream);
  DCHECK_EQ(1, 0x1 & associatedStream);
  DCHECK_EQ(0, ~kUint31Mask & promisedStream);

  const auto dataLen = headers->computeChainDataLength();
  const auto frameLen = writeFrameHeader(queue,
                                         dataLen + kFramePushPromiseSize,
                                         FrameType::PUSH_PROMISE,
                                         endHeaders ? END_HEADERS : 0,
                                         associatedStream,
                                         padding,
                                         boost::none,
                                         nullptr);
  QueueAppender appender(&queue, frameLen);
  appender.writeBE<uint32_t>(promisedStream);
  queue.append(std::move(headers));
  writePadding(queue, padding);
  return kFrameHeaderSize + frameLen;
}

size_t
writePing(IOBufQueue& queue,
          uint64_t opaqueData,
          bool ack) noexcept {
  const auto frameLen = writeFrameHeader(queue,
                                         kFramePingSize,
                                         FrameType::PING,
                                         ack ? ACK : 0,
                                         0,
                                         kNoPadding,
                                         boost::none,
                                         nullptr);
  queue.append(&opaqueData, sizeof(opaqueData));
  return kFrameHeaderSize + frameLen;
}

size_t
writeGoaway(IOBufQueue& queue,
            uint32_t lastStreamID,
            ErrorCode errorCode,
            std::unique_ptr<IOBuf> debugData) noexcept {
  uint32_t debugLen = debugData ? debugData->computeChainDataLength() : 0;
  DCHECK_EQ(0, ~kLengthMask & debugLen);
  const auto frameLen = writeFrameHeader(queue,
                                         kFrameGoawaySize + debugLen,
                                         FrameType::GOAWAY,
                                         0,
                                         0,
                                         kNoPadding,
                                         boost::none,
                                         nullptr);
  QueueAppender appender(&queue, frameLen);
  appender.writeBE<uint32_t>(lastStreamID);
  appender.writeBE<uint32_t>(static_cast<uint32_t>(errorCode));
  queue.append(std::move(debugData));
  return kFrameHeaderSize + frameLen;
}

size_t
writeWindowUpdate(IOBufQueue& queue,
                  uint32_t stream,
                  uint32_t amount) noexcept {
  const auto frameLen = writeFrameHeader(queue,
                                         kFrameWindowUpdateSize,
                                         FrameType::WINDOW_UPDATE,
                                         0,
                                         stream,
                                         kNoPadding,
                                         boost::none,
                                         nullptr);
  DCHECK_EQ(0, ~kUint31Mask & amount);
  DCHECK_LT(0, amount);
  QueueAppender appender(&queue, kFrameWindowUpdateSize);
  appender.writeBE<uint32_t>(amount);
  return kFrameHeaderSize + frameLen;
}

size_t
writeContinuation(IOBufQueue& queue,
                  uint32_t stream,
                  bool endHeaders,
                  std::unique_ptr<IOBuf> headers,
                  boost::optional<uint8_t> padding) noexcept {
  DCHECK_NE(0, stream);
  const auto dataLen = headers->computeChainDataLength();
  const auto frameLen = writeFrameHeader(queue,
                                         dataLen,
                                         FrameType::CONTINUATION,
                                         endHeaders ? END_HEADERS : 0,
                                         stream,
                                         padding,
                                         boost::none,
                                         std::move(headers));
  writePadding(queue, padding);
  return kFrameHeaderSize + frameLen;
}

size_t
writeAltSvc(IOBufQueue& queue,
            uint32_t stream,
            uint32_t maxAge,
            uint16_t port,
            StringPiece protocol,
            StringPiece host,
            StringPiece origin) noexcept {
  const auto protoLen = protocol.size();
  const auto hostLen = host.size();
  const auto originLen = origin.size();
  const auto frameLen = protoLen + hostLen + originLen + kFrameAltSvcSizeBase;

  writeFrameHeader(queue, frameLen, FrameType::ALTSVC, 0, stream, kNoPadding,
                   boost::none, nullptr);
  QueueAppender appender(&queue, frameLen);
  appender.writeBE<uint32_t>(maxAge);
  appender.writeBE<uint16_t>(port);
  appender.writeBE<uint8_t>(protoLen);
  appender.push(reinterpret_cast<const uint8_t*>(protocol.data()), protoLen);
  appender.writeBE<uint8_t>(hostLen);
  appender.push(reinterpret_cast<const uint8_t*>(host.data()), hostLen);
  appender.push(reinterpret_cast<const uint8_t*>(origin.data()), originLen);
  return kFrameHeaderSize + frameLen;
}

const char* getFrameTypeString(FrameType type) {
  switch (type) {
    case FrameType::DATA: return "DATA";
    case FrameType::HEADERS: return "HEADERS";
    case FrameType::PRIORITY: return "PRIORITY";
    case FrameType::RST_STREAM: return "RST_STREAM";
    case FrameType::SETTINGS: return "SETTINGS";
    case FrameType::PUSH_PROMISE: return "PUSH_PROMISE";
    case FrameType::PING: return "PING";
    case FrameType::GOAWAY: return "GOAWAY";
    case FrameType::WINDOW_UPDATE: return "WINDOW_UPDATE";
    case FrameType::CONTINUATION: return "CONTINUATION";
    case FrameType::ALTSVC: return "ALTSVC";
    default:
      // can happen when type was cast from uint8_t
      return "Unknown";
  }
  LOG(FATAL) << "Unreachable";
  return "";
}

}}
