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

#include <boost/optional/optional.hpp>
#include <cstdint>
#include <deque>
#include <folly/Range.h>
#include <folly/io/Cursor.h>
#include <proxygen/lib/http/codec/ErrorCode.h>
#include <proxygen/lib/http/codec/SettingsId.h>
#include <proxygen/lib/utils/Export.h>
#include <string.h>

#include <proxygen/lib/http/codec/HTTP2Constants.h>

namespace proxygen { namespace http2 {

//////// Constants ////////

extern const uint8_t kMaxFrameType;
extern const boost::optional<uint8_t> kNoPadding;

//////// Types ////////

typedef boost::optional<uint8_t> Padding;

enum class FrameType: uint8_t {
  DATA = 0,
  HEADERS = 1,
  PRIORITY = 2,
  RST_STREAM = 3,
  SETTINGS = 4,
  PUSH_PROMISE = 5,
  PING = 6,
  GOAWAY = 7,
  WINDOW_UPDATE = 8,
  CONTINUATION = 9,
  ALTSVC = 10,  // not in current draft so frame type has not been assigned
};

enum Flags {
  ACK = 0x1,
  END_STREAM = 0x1,
  END_HEADERS = 0x4,
  PADDED = 0x8,
  PRIORITY = 0x20,
};

struct FrameHeader {
  uint32_t length; // only 24 valid bits
  uint32_t stream;
  FrameType type;
  uint8_t flags;
  uint16_t unused;
};

static_assert(sizeof(FrameHeader) == 12, "The maths are not working");

struct PriorityUpdate {
  uint32_t streamDependency;
  bool exclusive;
  uint8_t weight;
};

//////// Bonus Constant ////////

FB_EXPORT extern const PriorityUpdate DefaultPriority;

//////// Functions ////////

extern bool frameAffectsCompression(FrameType t);

/**
 * This function returns true if the padding bit is set in the header
 *
 * @param header The frame header.
 * @return true if the padding bit is set, false otherwise.
 */
extern bool frameHasPadding(const FrameHeader& header);

//// Parsing ////

/**
 * This function parses the common HTTP/2 frame header. This function
 * pulls kFrameHeaderSize bytes from the cursor, so the caller must check
 * that that amount is available.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header struct to populate.
 * @return Nothing if success. The connection error code if failure.
 */
extern ErrorCode
parseFrameHeader(folly::io::Cursor& cursor,
                 FrameHeader& header) noexcept;

/**
 * This function parses the section of the DATA frame after the common
 * frame header. It discards any padding and returns the body data in
 * outBuf. It pulls header.length bytes from the cursor, so it is the
 * caller's responsibility to ensure there is enough data available.
 *
 * @param cursor  The cursor to pull data from.
 * @param header  The frame header for the frame being parsed.
 * @param outBuf  The buf to fill with body data.
 * @param padding The number of padding bytes in this data frame
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parseData(folly::io::Cursor& cursor,
          FrameHeader header,
          std::unique_ptr<folly::IOBuf>& outBuf,
          uint16_t& padding) noexcept;

ErrorCode
parseDataBegin(folly::io::Cursor& cursor,
               FrameHeader header,
               size_t& parsed,
               uint16_t& outPadding) noexcept;

extern ErrorCode
parseDataEnd(folly::io::Cursor& cursor,
             const size_t bufLen,
             const size_t pendingDataFramePaddingBytes,
             size_t& toSkip) noexcept;

/**
 * This function parses the section of the HEADERS frame after the common
 * frame header. It discards any padding and returns the header data in
 * outBuf. It pulls header.length bytes from the cursor, so it is the
 * caller's responsibility to ensure there is enough data available.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header for the frame being parsed.
 * @param outPriority If PRIORITY flag is set, this will be filled with
 *                    the priority information from this frame.
 * @param outBuf The buf to fill with header data.
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parseHeaders(folly::io::Cursor& cursor,
             FrameHeader header,
             boost::optional<PriorityUpdate>& outPriority,
             std::unique_ptr<folly::IOBuf>& outBuf) noexcept;

/**
 * This function parses the section of the PRIORITY frame after the common
 * frame header. It pulls header.length bytes from the cursor, so it is the
 * caller's responsibility to ensure there is enough data available.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header for the frame being parsed.
 * @param outPriority On success, filled with the priority information
 *                    from this frame.
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parsePriority(folly::io::Cursor& cursor,
              FrameHeader header,
              PriorityUpdate& outPriority) noexcept;

/**
 * This function parses the section of the RST_STREAM frame after the
 * common frame header. It pulls header.length bytes from the cursor, so
 * it is the caller's responsibility to ensure there is enough data
 * available.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header for the frame being parsed.
 * @param outCode The error code received in the frame.
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parseRstStream(folly::io::Cursor& cursor,
               FrameHeader header,
               ErrorCode& outCode) noexcept;

/**
 * This function parses the section of the SETTINGS frame after the
 * common frame header. It pulls header.length bytes from the cursor, so
 * it is the caller's responsibility to ensure there is enough data
 * available.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header for the frame being parsed.
 * @param settings The settings received in this frame.
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parseSettings(folly::io::Cursor& cursor,
              FrameHeader header,
              std::deque<SettingPair>& settings) noexcept;

/**
 * This function parses the section of the PUSH_PROMISE frame after the
 * common frame header. It pulls header.length bytes from the cursor, so
 * it is the caller's responsibility to ensure there is enough data
 * available.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header for the frame being parsed.
 * @param outPromisedStream The id of the stream promised by the remote.
 * @param outBuf The buffer to fill with header data.
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parsePushPromise(folly::io::Cursor& cursor,
                 FrameHeader header,
                 uint32_t& outPromisedStream,
                 std::unique_ptr<folly::IOBuf>& outBuf) noexcept;

/**
 * This function parses the section of the PING frame after the common
 * frame header. It pulls header.length bytes from the cursor, so it is
 * the caller's responsibility to ensure there is enough data available.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header for the frame being parsed.
 * @param outData The opaque data from the ping frame
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parsePing(folly::io::Cursor& cursor,
          FrameHeader header,
          uint64_t& outData) noexcept;

/**
 * This function parses the section of the GOAWAY frame after the common
 * frame header.  It pulls header.length bytes from the cursor, so
 * it is the caller's responsibility to ensure there is enough data
 * available.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header for the frame being parsed.
 * @param outLastStreamID The last stream id accepted by the remote.
 * @param outCode The error code received in the frame.
 * @param outDebugData Additional debug-data in the frame, if any
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parseGoaway(folly::io::Cursor& cursor,
            FrameHeader header,
            uint32_t& outLastStreamID,
            ErrorCode& outCode,
            std::unique_ptr<folly::IOBuf>& outDebugData) noexcept;

/**
 * This function parses the section of the WINDOW_UPDATE frame after the
 * common frame header. The caller must ensure there is header.length
 * bytes available in the cursor.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header for the frame being parsed.
 * @param outAmount The amount to increment the stream's window by.
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parseWindowUpdate(folly::io::Cursor& cursor,
                  FrameHeader header,
                  uint32_t& outAmount) noexcept;

/**
 * This function parses the section of the CONTINUATION frame after the
 * common frame header. The caller must ensure there is header.length
 * bytes available in the cursor.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header for the frame being parsed.
 * @param outBuf The buffer to fill with header data.
 * @param outAmount The amount to increment the stream's window by.
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parseContinuation(folly::io::Cursor& cursor,
                  FrameHeader header,
                  std::unique_ptr<folly::IOBuf>& outBuf) noexcept;

/**
 * This function parses the section of the ALTSVC frame after the
 * common frame header. The caller must ensure there is header.length
 * bytes available in the cursor.
 *
 * @param cursor The cursor to pull data from.
 * @param header The frame header for the frame being parsed.
 * @param outMaxAge The max age field.
 * @param outPort The port the alternative service is on.
 * @param outProtocol The alternative service protocol string.
 * @param outHost The alternative service host name.
 * @param outOrigin The origin the alternative service is applicable to.
 * @return NO_ERROR for successful parse. The connection error code to
 *         return in a GOAWAY frame if failure.
 */
extern ErrorCode
parseAltSvc(folly::io::Cursor& cursor,
            FrameHeader header,
            uint32_t& outMaxAge,
            uint32_t& outPort,
            std::string& outProtocol,
            std::string& outHost,
            std::string& outOrigin) noexcept;

//// Egress ////

/**
 * Generate an entire DATA frame, including the common frame header.
 * The combined length of the data buffer, the padding, and the padding
 * length MUST NOT exceed 2^14 - 1, which is kMaxFramePayloadLength.
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param data The body data to write out, can be nullptr for 0 length
 * @param stream The stream identifier of the DATA frame.
 * @param padding If not kNoPadding, adds 1 byte pad len and @padding pad bytes
 * @param endStream True iff this frame ends the stream.
 * @return The number of bytes written to writeBuf.
 */
extern size_t
writeData(folly::IOBufQueue& writeBuf,
          std::unique_ptr<folly::IOBuf> data,
          uint32_t stream,
          boost::optional<uint8_t> padding,
          bool endStream) noexcept;

/**
 * Generate an entire HEADERS frame, including the common frame
 * header. The combined length of
 * the data buffer and the padding and priority fields MUST NOT exceed
 * 2^14 - 1, which is kMaxFramePayloadLength.
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param headers The encoded headers data to write out.
 * @param stream The stream identifier of the DATA frame.
 * @param priority If present, the priority depedency information to
 *                 update the stream with.
 * @param padding If not kNoPadding, adds 1 byte pad len and @padding pad bytes
 * @param endStream True iff this frame ends the stream.
 * @param endHeaders True iff no CONTINUATION frames will follow this frame.
 * @return The number of bytes written to writeBuf.
 */
extern size_t
writeHeaders(folly::IOBufQueue& writeBuf,
             std::unique_ptr<folly::IOBuf> headers,
             uint32_t stream,
             boost::optional<PriorityUpdate> priority,
             boost::optional<uint8_t> padding,
             bool endStream,
             bool endHeaders) noexcept;

/**
 * Generate an entire PRIORITY frame, including the common frame header.
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param stream The stream identifier of the DATA frame.
 * @param priority The priority depedency information to update the stream with.
 * @return The number of bytes written to writeBuf.
 */
extern size_t
writePriority(folly::IOBufQueue& writeBuf,
              uint32_t stream,
              PriorityUpdate priority) noexcept;

/**
 * Generate an entire RST_STREAM frame, including the common frame
 * header.
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param stream The identifier of the stream to reset.
 * @param errorCode The error code returned in the frame.
 * @return The number of bytes written to writeBuf.
 */
extern size_t
writeRstStream(folly::IOBufQueue& writeBuf,
               uint32_t stream,
               ErrorCode errorCode) noexcept;

/**
 * Generate an entire SETTINGS frame, including the common frame
 * header.
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param settings The settings to send
 * @return The number of bytes written to writeBuf.
 */
extern size_t
writeSettings(folly::IOBufQueue& writeBuf,
              const std::deque<SettingPair>& settings);

/**
 * Writes an entire empty SETTINGS frame, including the common frame
 * header. No settings can be transmitted with this frame.
 */
extern size_t
writeSettingsAck(folly::IOBufQueue& writeBuf);

/**
 * Writes an entire PUSH_PROMISE frame, including the common frame
 * header.
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param associatedStream The identifier of the stream the promised
 *                         stream is associated with.
 * @param promisedStream The identifier of the promised stream.
 * @param headers The encoded headers to include in the push promise frame.
 * @param padding If not kNoPadding, adds 1 byte pad len and @padding pad bytes
 * @param endHeaders True iff no CONTINUATION frames will follow this frame.
 * @return The number of bytes written to writeBuf/
 */
extern size_t
writePushPromise(folly::IOBufQueue& writeBuf,
                 uint32_t associatedStream,
                 uint32_t promisedStream,
                 std::unique_ptr<folly::IOBuf> headers,
                 boost::optional<uint8_t> padding,
                 bool endHeaders) noexcept;
/**
 * Generate an entire PING frame, including the common frame header.
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param data The opaque data to include.
 * @param ack True iff this is a ping response.
 * @return The number of bytes written to writeBuf.
 */
extern size_t
writePing(folly::IOBufQueue& writeBuf,
          uint64_t data,
          bool ack) noexcept;

/**
 * Generate an entire GOAWAY frame, including the common frame
 * header. We do not implement the optional opaque data.
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param lastStreamID The identifier of the last stream accepted.
 * @param errorCode The error code returned in the frame.
 * @param debugData Optional debug information to add to the frame
 * @return The number of bytes written to writeBuf.
 */
extern size_t
writeGoaway(folly::IOBufQueue& writeBuf,
            uint32_t lastStreamID,
            ErrorCode errorCode,
            std::unique_ptr<folly::IOBuf> debugData = nullptr) noexcept;

/**
 * Generate an entire WINDOW_UPDATE frame, including the common frame
 * header. |amount| MUST be between 1 to 2^31 - 1 inclusive
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param stream The stream to send a WINDOW_UPDATE on
 * @param amount The number of bytes to AK
 * @return The number of bytes written to writeBuf.
 */
extern size_t
writeWindowUpdate(folly::IOBufQueue& writeBuf,
                  uint32_t stream,
                  uint32_t amount) noexcept;

/**
 * Generate an entire CONTINUATION frame, including the common frame
 * header. The combined length of the data buffer and the padding MUST NOT
 * exceed 2^14 - 3, which is kMaxFramePayloadLength minus the two bytes to
 * encode the length of the padding.
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param stream The stream identifier of the DATA frame.
 * @param endHeaders True iff more CONTINUATION frames will follow.
 * @param headers The encoded headers data to write out.
 * @param padding If not kNoPadding, adds 1 byte pad len and @padding pad bytes
 * @return The number of bytes written to writeBuf.
 */
extern size_t
writeContinuation(folly::IOBufQueue& queue,
                  uint32_t stream,
                  bool endHeaders,
                  std::unique_ptr<folly::IOBuf> headers,
                  boost::optional<uint8_t> padding) noexcept;
/**
 * Generate an entire ALTSVC frame, including the common frame
 * header.
 *
 * @param writeBuf The output queue to write to. It may grow or add
 *                 underlying buffers inside this function.
 * @param stream The stream to do Alt-Svc on. May be zero.
 * @param maxAge The max age field.
 * @param port The port the alternative service is on.
 * @param protocol The alternative service protocol string.
 * @param host The alternative service host name.
 * @param origin The origin the alternative service is applicable to.
 * @return The number of bytes written to writeBuf.
 */
extern size_t
writeAltSvc(folly::IOBufQueue& writeBuf,
            uint32_t stream,
            uint32_t maxAge,
            uint16_t port,
            folly::StringPiece protocol,
            folly::StringPiece host,
            folly::StringPiece origin) noexcept;

/**
 * Get the string representation of the given FrameType
 *
 * @param type frame type
 *
 * @return string representation of the frame type
 */
extern const char* getFrameTypeString(FrameType type);
}}
