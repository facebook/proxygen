/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <deque>
#include <folly/SocketAddress.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncTimeout.h>
#include <proxygen/lib/utils/Time.h>
#include <thrift/lib/cpp/async/TAsyncTransport.h>

class TestAsyncTransport : public apache::thrift::async::TAsyncTransport,
                           private folly::AsyncTimeout {
 public:
  class WriteEvent {
   public:
    static std::shared_ptr<WriteEvent> newEvent(const struct iovec* vec,
                                                  size_t count);

    proxygen::TimePoint getTime() const {
      return time_;
    }
    const struct iovec* getIoVec() const {
      return vec_;
    }
    size_t getCount() const {
      return count_;
    }

   private:
    static void destroyEvent(WriteEvent* event);

    WriteEvent(proxygen::TimePoint time, size_t count);
    ~WriteEvent();

    proxygen::TimePoint time_;
    size_t count_;
    struct iovec vec_[];
  };

  explicit TestAsyncTransport(folly::EventBase* eventBase);

  // TAsyncTransport methods
  void setReadCB(AsyncTransportWrapper::ReadCallback* callback) override;
  ReadCallback* getReadCallback() const override;
  void write(AsyncTransportWrapper::WriteCallback* callback,
             const void* buf, size_t bytes,
             apache::thrift::async::WriteFlags flags =
             apache::thrift::async::WriteFlags::NONE) override;
  void writev(AsyncTransportWrapper::WriteCallback* callback,
              const struct iovec* vec, size_t count,
              apache::thrift::async::WriteFlags flags =
              apache::thrift::async::WriteFlags::NONE) override;
  void writeChain(AsyncTransportWrapper::WriteCallback* callback,
                  std::unique_ptr<folly::IOBuf>&& iob,
                  apache::thrift::async::WriteFlags flags =
                  apache::thrift::async::WriteFlags::NONE) override;
  void close() override;
  void closeNow() override;
  void shutdownWrite() override;
  void shutdownWriteNow() override;
  void getPeerAddress(folly::SocketAddress* addr)
    const override;
  void getLocalAddress(folly::SocketAddress* addr)
    const override;
  bool good() const override;
  bool readable() const override;
  bool connecting() const override;
  bool error() const override;
  void attachEventBase(folly::EventBase* eventBase) override;
  void detachEventBase() override;
  bool isDetachable() const override;
  folly::EventBase* getEventBase() const override;
  void setSendTimeout(uint32_t milliseconds) override;
  uint32_t getSendTimeout() const override;

  // Methods to control read events
  void addReadEvent(const void* buf, size_t buflen,
                    std::chrono::milliseconds delayFromPrevious);
  void addReadEvent(folly::IOBufQueue& chain,
                    std::chrono::milliseconds delayFromPrevious);
  void addReadEvent(const char* buf,
                    std::chrono::milliseconds delayFromPrevious);
  void addReadEOF(std::chrono::milliseconds delayFromPrevious);
  void addReadError(const folly::AsyncSocketException& ex,
                    std::chrono::milliseconds delayFromPrevious);
  void startReadEvents();

  void pauseWrites();
  void resumeWrites();

  // Methods to get the data written to this transport
  std::deque< std::shared_ptr<WriteEvent> >* getWriteEvents() {
    return &writeEvents_;
  }

  uint32_t getEORCount() {
    return eorCount_;
  }

  uint32_t getCorkCount() {
    return corkCount_;
  }

  size_t getAppBytesWritten() const { return 0; }
  size_t getRawBytesWritten() const { return 0; }
  size_t getAppBytesReceived() const { return 0; }
  size_t getRawBytesReceived() const { return 0; }
  bool isEorTrackingEnabled() const { return false; }
  void setEorTracking(bool) { return; }

 private:
  enum StateEnum {
    kStateOpen,
    kStatePaused,
    kStateClosed,
    kStateError,
  };

  class ReadEvent;

  bool writesAllowed() const { return writeState_ == kStateOpen ||
      writeState_ == kStatePaused; }

  // Forbidden copy constructor and assignment opererator
  TestAsyncTransport(TestAsyncTransport const&);
  TestAsyncTransport& operator=(TestAsyncTransport const&);

  void addReadEvent(const std::shared_ptr<ReadEvent>& event);
  void scheduleNextReadEvent(proxygen::TimePoint now);
  void fireNextReadEvent();
  void fireOneReadEvent();
  void failPendingWrites();

  // AsyncTimeout methods
  virtual void timeoutExpired() noexcept;

  folly::EventBase* eventBase_;
  folly::AsyncTransportWrapper::ReadCallback* readCallback_;
  uint32_t sendTimeout_;

  proxygen::TimePoint prevReadEventTime_{};
  proxygen::TimePoint nextReadEventTime_{};
  StateEnum readState_;
  StateEnum writeState_;
  std::deque< std::shared_ptr<ReadEvent> > readEvents_;
  std::deque< std::shared_ptr<WriteEvent> > writeEvents_;
  std::deque< std::pair<std::shared_ptr<WriteEvent>, AsyncTransportWrapper::WriteCallback*>>
    pendingWriteEvents_;

  uint32_t eorCount_{0};
  uint32_t corkCount_{0};
};
