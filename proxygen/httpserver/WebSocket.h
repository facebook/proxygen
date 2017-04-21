#pragma once

#include <folly/Memory.h>
#include <proxygen/httpserver/RequestHandler.h>

namespace proxygen {

  class WebSocket;
  class WebSocketFrame
  {
  public:
    enum class FrameType {
      CONTINUE = 0x0,
      TEXT = 0x1,
      BINARY = 0x2,
      CLOSE = 0x8,
      PING = 0x9,
      PONG = 0xA
    };
    FrameType frameType;
    bool endOfMessage;
    unique_ptr<folly::IOBuf> payload;
  };

  class WebSocket
  {
  public:
    static bool isWebSocketRequest(const HTTPMessage& requestHeaders)  noexcept;
    static unique_ptr<WebSocket> acceptWebSocket(ResponseHandler*downstream, const HTTPMessage& requestHeaders, std::function<void(std::unique_ptr<WebSocketFrame>)> onData, const std::string protocol = "");
    void processData(unique_ptr<folly::IOBuf> data) noexcept;
    void sendFrame(WebSocketFrame::FrameType type, unique_ptr<folly::IOBuf> payload, bool endOfMessage) noexcept;
  private:
    enum class WebSocketState
    {
      ReadingInitialHeader,
      ReadingSize16,
      ReadingSize64,
      ReadingMask,
      ReadingPayload
    };
    struct WebSocketFrameHeader
    {
      bool fin;
      int opcode;
      bool masked;
      int length1;
    };
    ResponseHandler* downstream_;
    std::function<void(std::unique_ptr<WebSocketFrame>)> onData_;
    uint8_t header_[10];
    uint8_t mask_[4];
    int maskIndex_ = 0;
    WebSocketState state_;
    size_t headerFillProgress_;
    size_t maskFillProgress_;
    uint64_t currentFrameLength_;
    size_t currentFrameProgress_;
    WebSocketFrameHeader frameHeader_;
    unique_ptr<folly::IOBuf> currentFrame_;
    bool closeFrameReceived_ = false;
    explicit WebSocket(ResponseHandler* downstream, std::function<void(std::unique_ptr<WebSocketFrame>)> onData)  noexcept;
    static bool read(folly::IOBuf& ioBuf, void*buffer, size_t max, size_t& progress);
    void resetState();
  };
}