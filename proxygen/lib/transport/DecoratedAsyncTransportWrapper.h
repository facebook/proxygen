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

#include <folly/io/async/AsyncTransport.h>

namespace proxygen {

/**
 * Convenience class so that AsyncTransportWrapper can be decorated without
 * having to redefine every single method.
 */
template<class T>
class DecoratedAsyncTransportWrapper : public folly::AsyncTransportWrapper {
 public:
  explicit DecoratedAsyncTransportWrapper(typename T::UniquePtr transport):
    transport_(std::move(transport)) {}

  AsyncTransportWrapper* getWrappedTransport() override {
    return transport_.get();
  }

  // folly::AsyncTransportWrapper
  virtual ReadCallback* getReadCallback() const override {
    return transport_->getReadCallback();
  }

  virtual void setReadCB(
      folly::AsyncTransportWrapper::ReadCallback* callback) override {
    transport_->setReadCB(callback);
  }

  virtual void write(
      folly::AsyncTransportWrapper::WriteCallback* callback,
      const void* buf,
      size_t bytes,
      folly::WriteFlags flags = folly::WriteFlags::NONE,
      folly::AsyncTransportWrapper::BufferCallback* bufCB = nullptr) override {
    transport_->write(callback, buf, bytes, flags, bufCB);
  }

  virtual void writeChain(
      folly::AsyncTransportWrapper::WriteCallback* callback,
      std::unique_ptr<folly::IOBuf>&& buf,
      folly::WriteFlags flags = folly::WriteFlags::NONE,
      folly::AsyncTransportWrapper::BufferCallback* bufCB = nullptr) override {
    transport_->writeChain(callback, std::move(buf), flags, bufCB);
  }

  virtual void writev(
      folly::AsyncTransportWrapper::WriteCallback* callback,
      const iovec* vec,
      size_t bytes,
      folly::WriteFlags flags = folly::WriteFlags::NONE,
      folly::AsyncTransportWrapper::BufferCallback* bufCB = nullptr) override {
    transport_->writev(callback, vec, bytes, flags, bufCB);
  }

  // folly::AsyncSocketBase
  virtual folly::EventBase* getEventBase() const override {
    return transport_->getEventBase();
  }

  // folly::AsyncTransport
  virtual void attachEventBase(folly::EventBase* eventBase) override {
    transport_->attachEventBase(eventBase);
  }

  virtual void close() override {
    transport_->close();
  }

  virtual void closeNow() override {
    transport_->closeNow();
  }

  virtual bool connecting() const override {
    return transport_->connecting();
  }

  virtual void detachEventBase() override {
    transport_->detachEventBase();
  }

  virtual bool error() const override {
    return transport_->error();
  }

  virtual size_t getAppBytesReceived() const override {
    return transport_->getAppBytesReceived();
  }

  virtual size_t getAppBytesWritten() const override {
    return transport_->getAppBytesWritten();
  }

  virtual void getLocalAddress(folly::SocketAddress* address) const override {
    return transport_->getLocalAddress(address);
  }

  virtual void getPeerAddress(folly::SocketAddress* address) const override {
    return transport_->getPeerAddress(address);
  }

  virtual size_t getRawBytesReceived() const override {
    return transport_->getRawBytesReceived();
  }

  virtual size_t getRawBytesWritten() const override {
    return transport_->getRawBytesWritten();
  }

  virtual uint32_t getSendTimeout() const override {
    return transport_->getSendTimeout();
  }

  virtual bool good() const override {
    return transport_->good();
  }

  virtual bool isDetachable() const override {
    return transport_->isDetachable();
  }

  virtual bool isEorTrackingEnabled() const override {
    return transport_->isEorTrackingEnabled();
  }

  virtual bool readable() const override {
    return transport_->readable();
  }

  virtual void setEorTracking(bool track) override {
    return transport_->setEorTracking(track);
  }

  virtual void setSendTimeout(uint32_t milliseconds) override {
    transport_->setSendTimeout(milliseconds);
  }

  virtual void shutdownWrite() override {
    transport_->shutdownWrite();
  }

  virtual void shutdownWriteNow() override {
    transport_->shutdownWriteNow();
  }

 protected:
  virtual ~DecoratedAsyncTransportWrapper() {}

  typename T::UniquePtr transport_;
};

}
