#include "WebSocket.h"
#include <proxygen/httpserver/ResponseHandler.h>
#include <stdint.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#if defined(__linux__)
#  include <endian.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#  include <sys/endian.h>
#elif defined(__OpenBSD__)
#  include <sys/types.h>
#  define be64toh(x) betoh64(x)
#  define h64tobe(x) htobe64(x)
#endif

using namespace std;
using namespace folly;

namespace proxygen {

  WebSocket::WebSocket(ResponseHandler* downstream, std::function<void(std::unique_ptr<WebSocketFrame>)> onData)  noexcept {
    downstream_ = downstream;
    onData_ = onData;
    resetState();
  }

  bool WebSocket::isWebSocketRequest(const HTTPMessage &requestHeaders) noexcept {
    auto headers = requestHeaders.getHeaders();
    return
      (headers.getSingleOrEmpty(HTTP_HEADER_CONNECTION).find("Upgrade", 0) != string::npos)
        && (headers.getSingleOrEmpty(HTTP_HEADER_UPGRADE).find("websocket", 0) != string::npos)
        && (headers.getNumberOfValues("Sec-WebSocket-Key") != 0)
        && (headers.getSingleOrEmpty("Sec-WebSocket-Version") == "13");
  }

  unique_ptr<WebSocket> WebSocket::acceptWebSocket(ResponseHandler*downstream, const HTTPMessage& requestHeaders, std::function<void(std::unique_ptr<WebSocketFrame>)> onData, const std::string protocol) {

    //Calculate value for Sec-WebSocket-Accept
    auto keyString = string(requestHeaders.getHeaders().getSingleOrEmpty("Sec-WebSocket-Key")).append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

    SHA_CTX shaContext;
    uint8_t sha1[SHA_DIGEST_LENGTH];
    SHA1_Init(&shaContext);
    SHA1_Update(&shaContext, keyString.c_str(), keyString.length());
    SHA1_Final(sha1, &shaContext);


    BIO *bmem, *b64;
    BUF_MEM *bptr;

    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, sha1, SHA_DIGEST_LENGTH);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    auto shaString = string(bptr->data, bptr->length - 1);
    BIO_free_all(b64);


    auto headers = HTTPMessage();
    headers.constructDirectResponse({1, 1}, 101, "Switching Protocols");
    headers.getHeaders().add(HTTP_HEADER_UPGRADE, "websocket");
    headers.getHeaders().add(HTTP_HEADER_CONNECTION, "Upgrade");
    headers.getHeaders().add("Sec-WebSocket-Accept", shaString);
    if(!protocol.empty())
      headers.getHeaders().add("Sec-WebSocket-Protocol", protocol);

    downstream->sendHeaders(headers);


    return unique_ptr<WebSocket>(new WebSocket(downstream, onData));
  }

  void WebSocket::resetState()
  {
    state_= WebSocketState::ReadingInitialHeader;
    headerFillProgress_ = 0;
    maskFillProgress_ = 0;
    currentFrameLength_ = 0;
    currentFrameProgress_ = 0;
    maskIndex_ = 0;
  }

  #define WEBSOCKET_INITIAL_HEADER_LENGTH 2
  #define WEBSOCKET_LEN16_LENGTH 4
  #define WEBSOCKET_LEN64_LENGTH 10

  #define WEBSOCKET_LEN16_CODE 126
  #define WEBSOCKET_LEN64_CODE 127

  void WebSocket::processData(unique_ptr<IOBuf> data) noexcept {
    auto nextBuffer = data.release();
    while(nextBuffer)
    {
      unique_ptr<IOBuf> currentBuffer;
      if(nextBuffer->next() == nextBuffer) {
        currentBuffer = unique_ptr<IOBuf>(nextBuffer);
        nextBuffer = nullptr;
      }
      else
      {
        auto tmp = nextBuffer;
        nextBuffer = nextBuffer->next();
        currentBuffer = tmp->unlink();
      }


      while (currentBuffer && currentBuffer->length() > 0)
      {
        switch (state_)
        {
          case WebSocketState::ReadingInitialHeader:
            if(read(*currentBuffer, &header_, WEBSOCKET_INITIAL_HEADER_LENGTH, headerFillProgress_))
            {
              frameHeader_.fin = (header_[0] & 0x80) != 0;
              frameHeader_.opcode = header_[0] & 0x0f;
              frameHeader_.masked = (header_[1] & 0x80) != 0;
              frameHeader_.length1 = header_[1] & 0x7f;


              if(frameHeader_.length1 == WEBSOCKET_LEN16_CODE)
                state_ = WebSocketState::ReadingSize16;
              else if(frameHeader_.length1 == WEBSOCKET_LEN64_CODE)
                state_ = WebSocketState::ReadingSize64;
              else {
                state_ = WebSocketState::ReadingMask;
                currentFrameLength_ = frameHeader_.length1;
              }
            }
            break;
          case WebSocketState::ReadingSize16:
            if(read(*currentBuffer, &header_, WEBSOCKET_LEN16_LENGTH, headerFillProgress_))
            {
              currentFrameLength_ = ntohs(*(uint16_t*)&header_[2]);
              state_ = WebSocketState::ReadingMask;
            }
            break;
          case WebSocketState::ReadingSize64:
            if(read(*currentBuffer, &header_, WEBSOCKET_LEN64_LENGTH, headerFillProgress_))
            {
              currentFrameLength_ = be64toh(*(uint64_t*)&header_[2]);
              state_ = WebSocketState::ReadingMask;
            }
            break;
          case WebSocketState::ReadingMask:
            if(!frameHeader_.masked)
              state_ = WebSocketState::ReadingPayload;
            else if(read(*currentBuffer, &mask_, sizeof(mask_), maskFillProgress_))
              state_ = WebSocketState::ReadingPayload;
            break;
          case WebSocketState::ReadingPayload:
            if(currentFrameProgress_ != currentFrameLength_)
            {
              auto toRead = std::min(currentFrameLength_ - currentFrameProgress_, currentBuffer->length());
              unique_ptr<IOBuf> appendBuffer;
              if(toRead == currentBuffer->length())
                appendBuffer = std::move(currentBuffer);
              else
              {
                appendBuffer = IOBuf::create(toRead);
                memcpy(appendBuffer->writableData(), currentBuffer->data(), toRead);
                currentBuffer->trimStart(toRead);
              }

              appendBuffer->unshare();
              if(frameHeader_.masked)
              {
                auto ptr = appendBuffer->writableData();
                auto len = appendBuffer->length();
                for(size_t c = 0; c<len; c++)
                {
                  ptr[c]^=mask_[maskIndex_];
                  maskIndex_++;
                  if(maskIndex_ == 4)
                    maskIndex_ = 0;
                }
              }

              if(currentFrame_)
                currentFrame_->prependChain(std::move(appendBuffer));
              else
                currentFrame_ = std::move(appendBuffer);
              currentFrameProgress_ += toRead;
            }
            //TODO: emulate virtual frame (endOfMessage = false) to avoid buffering too much payload
            if(currentFrameProgress_ == currentFrameLength_)
            {
              auto f = make_unique<WebSocketFrame>();
              f->frameType = (WebSocketFrame::FrameType)frameHeader_.opcode;
              f->endOfMessage = frameHeader_.fin;
              f->payload = std::move(currentFrame_);
              resetState();
              if(f->frameType==WebSocketFrame::FrameType::CLOSE)
                closeFrameReceived_ = true;

              //Handle ping-pong here
              if(f->frameType == WebSocketFrame::FrameType::PING) {
                if(!closeFrameReceived_)
                  sendFrame(WebSocketFrame::FrameType::PONG, std::move(f->payload), true);
              }
              else
                onData_(std::move(f));
            }
            break;
        }



      }
    }
  }

  bool WebSocket::read(folly::IOBuf& ioBuf, void*buffer, size_t max, size_t& progress)
  {
    auto toRead = std::min(ioBuf.length(), max - progress);
    memcpy(&((uint8_t*)buffer)[progress], ioBuf.data(), toRead);
    ioBuf.trimStart(toRead);
    progress += toRead;
    return progress == max;
  }

  void WebSocket::sendFrame(WebSocketFrame::FrameType type, unique_ptr<folly::IOBuf> payload, bool endOfMessage) noexcept {
    uint8_t header[10];
    size_t headerLength;
    uint8_t length1;
    auto len = payload ? payload->computeChainDataLength() : 0;
    if(len <= 125) {
      headerLength = WEBSOCKET_INITIAL_HEADER_LENGTH;
      length1 = (uint8_t)len;
    } else if(len <= 0xffff) {
      headerLength = WEBSOCKET_LEN16_LENGTH;
      length1 = WEBSOCKET_LEN16_CODE;
      *(uint16_t*)&header[2] = htons((uint16_t)len);
    } else {
      headerLength = WEBSOCKET_LEN64_LENGTH;
      length1 = WEBSOCKET_LEN64_CODE;
      *(uint64_t*)&header_[2] = htobe64(len);
    }

    header[0] =(uint8_t)( ((endOfMessage?1u:0u) << 7) |((uint8_t)(type)&0xf));
    header[1] = length1; //not masked, so just using the value

    auto buffer = IOBuf::copyBuffer(header, headerLength);
    if(payload)
        buffer->prependChain(std::move(payload));
    downstream_->sendBody(std::move(buffer));
  }

}