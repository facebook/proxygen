/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/TransportFilter.h>



using namespace folly;

namespace proxygen {

// AsyncTransportWrapper::ReadCallback methods
void PassThroughTransportFilter::getReadBuffer(void** bufReturn,
                                               size_t* lenReturn) {
  callback_->getReadBuffer(bufReturn, lenReturn);
}

void PassThroughTransportFilter::readDataAvailable(size_t len) noexcept {
  callback_->readDataAvailable(len);
}

void PassThroughTransportFilter::readEOF() noexcept {
  callback_->readEOF();
  destroy();
}

void PassThroughTransportFilter::readErr(
    const AsyncSocketException& ex) noexcept {
  callback_->readErr(ex);
  destroy();
}

// AsyncTransport methods

void PassThroughTransportFilter::setReadCB(
    AsyncTransportWrapper::ReadCallback* callback) {
  // Important! The filter must call setCallbackInternal in its base class and
  // it must not forward the call.
  setCallbackInternal(callback);
}

AsyncTransportWrapper::ReadCallback*
PassThroughTransportFilter::getReadCallback() const {
  return call_->getReadCallback();
}

void PassThroughTransportFilter::write(
    AsyncTransportWrapper::WriteCallback* callback,
    const void* buf, size_t bytes, WriteFlags flags) {
  call_->write(callback, buf, bytes, flags);
}

void PassThroughTransportFilter::writev(
    AsyncTransportWrapper::WriteCallback* callback, const iovec* vec, size_t count,
    WriteFlags flags) {
  call_->writev(callback, vec, count, flags);
}

void PassThroughTransportFilter::writeChain(
    AsyncTransportWrapper::WriteCallback* callback,
    std::unique_ptr<folly::IOBuf>&& iob, WriteFlags flags) {
  call_->writeChain(callback, std::move(iob), flags);
}

void PassThroughTransportFilter::close() {
  call_->close();
  // wait for readEOF() to call destroy()
}

void PassThroughTransportFilter::closeNow() {
  call_->closeNow();
  // wait for readEOF() to call destroy()
}

void PassThroughTransportFilter::closeWithReset() {
  call_->closeWithReset();
  // wait for readEOF() to call destroy()
}

void PassThroughTransportFilter::shutdownWrite() {
  call_->shutdownWrite();
}

void PassThroughTransportFilter::shutdownWriteNow() {
  call_->shutdownWriteNow();
}

bool PassThroughTransportFilter::good() const {
  return call_->good();
}

bool PassThroughTransportFilter::readable() const {
  return call_->readable();
}

bool PassThroughTransportFilter::connecting() const {
  return call_->connecting();
}

bool PassThroughTransportFilter::error() const {
  return call_->error();
}

void PassThroughTransportFilter::attachEventBase(EventBase* eventBase) {
  call_->attachEventBase(eventBase);
}

void PassThroughTransportFilter::detachEventBase() {
  call_->detachEventBase();
}

bool PassThroughTransportFilter::isDetachable() const {
  return call_->isDetachable();
}

EventBase* PassThroughTransportFilter::getEventBase() const {
  return call_->getEventBase();
}

void PassThroughTransportFilter::setSendTimeout(uint32_t milliseconds) {
  call_->setSendTimeout(milliseconds);
}

uint32_t PassThroughTransportFilter::getSendTimeout() const {
  return call_->getSendTimeout();
}

void PassThroughTransportFilter::getLocalAddress(
    folly::SocketAddress* address) const {
  call_->getLocalAddress(address);
}

void PassThroughTransportFilter::getPeerAddress(
    folly::SocketAddress* address) const {
  call_->getPeerAddress(address);
}

void PassThroughTransportFilter::setEorTracking(bool track) {
  call_->setEorTracking(track);
}

size_t PassThroughTransportFilter::getAppBytesWritten() const {
  return call_->getAppBytesWritten();
}
size_t PassThroughTransportFilter::getRawBytesWritten() const {
  return call_->getRawBytesWritten();
}
size_t PassThroughTransportFilter::getAppBytesReceived() const {
  return call_->getAppBytesReceived();
}
size_t PassThroughTransportFilter::getRawBytesReceived() const {
  //new PassThroughTransportFilter();
  return call_->getRawBytesReceived();
}

}
