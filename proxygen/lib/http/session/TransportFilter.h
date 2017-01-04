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

#include <folly/io/async/EventBase.h>
#include <proxygen/lib/utils/FilterChain.h>
#include <folly/io/async/AsyncTransport.h>

namespace proxygen {

typedef GenericFilter<
  folly::AsyncTransportWrapper,
  folly::AsyncTransportWrapper::ReadCallback,
  &folly::AsyncTransportWrapper::setReadCB,
  true,
  folly::AsyncTransportWrapper::Destructor> TransportFilter;

/**
 * An implementation of Transport that passes through all calls and also
 * properly calls setCallback(). This is useful to subclass if you aren't
 * interested in intercepting every function. See AsyncTransport.h for
 * documentation on these methods
 */
class PassThroughTransportFilter: public TransportFilter {
 public:
  /**
   * By default, the filter gets both calls and callbacks
   */
  explicit PassThroughTransportFilter(bool calls = true,
                                      bool callbacks = true):
      TransportFilter(calls, callbacks) {}

  // AsyncTransportWrapper::ReadCallback methods

  void getReadBuffer(void** bufReturn, size_t* lenReturn) override;

  void readDataAvailable(size_t len) noexcept override;

  void readEOF() noexcept override;

  void readErr(const folly::AsyncSocketException& ex)
    noexcept override;

  // AsyncTransport methods

  void setReadCB(
    folly::AsyncTransportWrapper::ReadCallback* callback) override;

  folly::AsyncTransportWrapper::ReadCallback* getReadCallback()
    const override;

  void write(folly::AsyncTransportWrapper::WriteCallback* callback,
             const void* buf, size_t bytes,
             folly::WriteFlags flags) override;

  void writev(folly::AsyncTransportWrapper::WriteCallback* callback,
              const iovec* vec, size_t count,
              folly::WriteFlags flags) override;

  void writeChain(
    folly::AsyncTransportWrapper::WriteCallback* callback,
    std::unique_ptr<folly::IOBuf>&& iob,
    folly::WriteFlags flags) override;

  void close() override;

  void closeNow() override;

  void closeWithReset() override;

  void shutdownWrite() override;

  void shutdownWriteNow() override;

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

  void getLocalAddress(
  folly::SocketAddress* address) const override;

  void getPeerAddress(
  folly::SocketAddress* address) const override;

  void setEorTracking(bool track) override;

  size_t getAppBytesWritten() const override;
  size_t getRawBytesWritten() const override;
  size_t getAppBytesReceived() const override;
  size_t getRawBytesReceived() const override;
};

typedef FilterChain<
  folly::AsyncTransportWrapper,
  folly::AsyncTransportWrapper::ReadCallback,
  PassThroughTransportFilter,
  &folly::AsyncTransportWrapper::setReadCB,
  false> TransportFilterChain;

}
