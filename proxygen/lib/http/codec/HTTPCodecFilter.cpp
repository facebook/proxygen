/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/HTTPCodecFilter.h>

namespace proxygen {

// HTTPCodec::Callback methods
void PassThroughHTTPCodecFilter::onMessageBegin(StreamID stream,
                                                HTTPMessage* msg) {
  callback_->onMessageBegin(stream, msg);
}

void PassThroughHTTPCodecFilter::onPushMessageBegin(StreamID stream,
                                                    StreamID assocStream,
                                                    HTTPMessage* msg) {
  callback_->onPushMessageBegin(stream, assocStream, msg);
}

void PassThroughHTTPCodecFilter::onHeadersComplete(
    StreamID stream,
    std::unique_ptr<HTTPMessage> msg) {
  callback_->onHeadersComplete(stream, std::move(msg));
}

void PassThroughHTTPCodecFilter::onBody(StreamID stream,
                                        std::unique_ptr<folly::IOBuf> chain,
                                        uint16_t padding) {
  callback_->onBody(stream, std::move(chain), padding);
}

void PassThroughHTTPCodecFilter::onChunkHeader(StreamID stream,
                                               size_t length) {
  callback_->onChunkHeader(stream, length);
}

void PassThroughHTTPCodecFilter::onChunkComplete(StreamID stream) {
  callback_->onChunkComplete(stream);
}

void PassThroughHTTPCodecFilter::onTrailersComplete(
    StreamID stream,
    std::unique_ptr<HTTPHeaders> trailers) {
  callback_->onTrailersComplete(stream, std::move(trailers));
}

void PassThroughHTTPCodecFilter::onMessageComplete(StreamID stream,
                                                   bool upgrade) {
  callback_->onMessageComplete(stream, upgrade);
}

void PassThroughHTTPCodecFilter::onFrameHeader(
    uint32_t stream_id,
    uint8_t flags,
    uint32_t length,
    uint16_t version) {
  callback_->onFrameHeader(stream_id, flags, length, version);
}

void PassThroughHTTPCodecFilter::onError(
    StreamID stream,
    const HTTPException& error,
    bool newStream) {
  callback_->onError(stream, error, newStream);
}

void PassThroughHTTPCodecFilter::onAbort(StreamID stream,
                                         ErrorCode code) {
  callback_->onAbort(stream, code);
}

void PassThroughHTTPCodecFilter::onGoaway(
  uint64_t lastGoodStreamID,
  ErrorCode code,
  std::unique_ptr<folly::IOBuf> debugData) {
  callback_->onGoaway(lastGoodStreamID, code, std::move(debugData));
}

void PassThroughHTTPCodecFilter::onPingRequest(uint64_t uniqueID) {
  callback_->onPingRequest(uniqueID);
}

void PassThroughHTTPCodecFilter::onPingReply(uint64_t uniqueID) {
  callback_->onPingReply(uniqueID);
}

void PassThroughHTTPCodecFilter::onWindowUpdate(StreamID stream,
                                                uint32_t amount) {
  callback_->onWindowUpdate(stream, amount);
}

void PassThroughHTTPCodecFilter::onSettings(const SettingsList& settings) {
  callback_->onSettings(settings);
}

void PassThroughHTTPCodecFilter::onSettingsAck() {
  callback_->onSettingsAck();
}

void PassThroughHTTPCodecFilter::onPriority(
  StreamID stream,
  const HTTPMessage::HTTPPriority& pri) {
  callback_->onPriority(stream, pri);
}

bool PassThroughHTTPCodecFilter::onNativeProtocolUpgrade(
  StreamID streamID, CodecProtocol protocol, const std::string& protocolString,
  HTTPMessage& msg) {
  return callback_->onNativeProtocolUpgrade(streamID, protocol, protocolString,
                                            msg);
}

uint32_t PassThroughHTTPCodecFilter::numOutgoingStreams() const {
  return callback_->numOutgoingStreams();
}

uint32_t PassThroughHTTPCodecFilter::numIncomingStreams() const {
  return callback_->numIncomingStreams();
}

// PassThroughHTTPCodec methods
CodecProtocol PassThroughHTTPCodecFilter::getProtocol() const {
  return call_->getProtocol();
}

TransportDirection PassThroughHTTPCodecFilter::getTransportDirection() const {
  return call_->getTransportDirection();
}

bool PassThroughHTTPCodecFilter::supportsStreamFlowControl() const {
  return call_->supportsStreamFlowControl();
}

bool PassThroughHTTPCodecFilter::supportsSessionFlowControl() const {
  return call_->supportsSessionFlowControl();
}

HTTPCodec::StreamID PassThroughHTTPCodecFilter::createStream() {
  return call_->createStream();
}

void PassThroughHTTPCodecFilter::setCallback(HTTPCodec::Callback* callback) {
  setCallbackInternal(callback);
}

bool PassThroughHTTPCodecFilter::isBusy() const {
  return call_->isBusy();
}

void PassThroughHTTPCodecFilter::setParserPaused(bool paused) {
  call_->setParserPaused(paused);
}

size_t PassThroughHTTPCodecFilter::onIngress(const folly::IOBuf& buf) {
  return call_->onIngress(buf);
}

void PassThroughHTTPCodecFilter::onIngressEOF() {
  call_->onIngressEOF();
}

bool PassThroughHTTPCodecFilter::onIngressUpgradeMessage(
  const HTTPMessage& msg) {
  return call_->onIngressUpgradeMessage(msg);
}

bool PassThroughHTTPCodecFilter::isReusable() const {
  return call_->isReusable();
}

bool PassThroughHTTPCodecFilter::isWaitingToDrain() const {
  return call_->isWaitingToDrain();
}

bool PassThroughHTTPCodecFilter::closeOnEgressComplete() const {
  return call_->closeOnEgressComplete();
}

bool PassThroughHTTPCodecFilter::supportsParallelRequests() const {
  return call_->supportsParallelRequests();
}

bool PassThroughHTTPCodecFilter::supportsPushTransactions() const {
  return call_->supportsPushTransactions();
}

size_t PassThroughHTTPCodecFilter::generateConnectionPreface(
  folly::IOBufQueue& writeBuf) {
  return call_->generateConnectionPreface(writeBuf);
}


void PassThroughHTTPCodecFilter::generateHeader(folly::IOBufQueue& writeBuf,
                                                StreamID stream,
                                                const HTTPMessage& msg,
                                                StreamID assocStream,
                                                bool eom,
                                                HTTPHeaderSize* size) {
  return call_->generateHeader(writeBuf, stream, msg, assocStream, eom, size);
}

size_t PassThroughHTTPCodecFilter::generateBody(
    folly::IOBufQueue& writeBuf,
    StreamID stream,
    std::unique_ptr<folly::IOBuf> chain,
    boost::optional<uint8_t> padding,
    bool eom) {
  return call_->generateBody(writeBuf, stream, std::move(chain), padding,
                             eom);
}

size_t PassThroughHTTPCodecFilter::generateChunkHeader(
    folly::IOBufQueue& writeBuf,
    StreamID stream,
    size_t length) {
  return call_->generateChunkHeader(writeBuf, stream, length);
}

size_t PassThroughHTTPCodecFilter::generateChunkTerminator(
    folly::IOBufQueue& writeBuf,
    StreamID stream) {
  return call_->generateChunkTerminator(writeBuf, stream);
}

size_t PassThroughHTTPCodecFilter::generateTrailers(
    folly::IOBufQueue& writeBuf,
    StreamID stream,
    const HTTPHeaders& trailers) {
  return call_->generateTrailers(writeBuf, stream, trailers);
}

size_t PassThroughHTTPCodecFilter::generateEOM(folly::IOBufQueue& writeBuf,
                                               StreamID stream) {
  return call_->generateEOM(writeBuf, stream);
}

size_t PassThroughHTTPCodecFilter::generateRstStream(
    folly::IOBufQueue& writeBuf,
    StreamID stream,
    ErrorCode code) {
  return call_->generateRstStream(writeBuf, stream, code);
}

size_t PassThroughHTTPCodecFilter::generateGoaway(
    folly::IOBufQueue& writeBuf,
    StreamID lastStream,
    ErrorCode statusCode,
    std::unique_ptr<folly::IOBuf> debugData) {
  return call_->generateGoaway(writeBuf, lastStream, statusCode,
                               std::move(debugData));
}

size_t PassThroughHTTPCodecFilter::generatePingRequest(
    folly::IOBufQueue& writeBuf) {
  return call_->generatePingRequest(writeBuf);
}

size_t PassThroughHTTPCodecFilter::generatePingReply(
    folly::IOBufQueue& writeBuf,
    uint64_t uniqueID) {
  return call_->generatePingReply(writeBuf, uniqueID);
}

size_t PassThroughHTTPCodecFilter::generateSettings(folly::IOBufQueue& buf) {
  return call_->generateSettings(buf);
}

size_t PassThroughHTTPCodecFilter::generateSettingsAck(folly::IOBufQueue& buf) {
  return call_->generateSettingsAck(buf);
}

size_t PassThroughHTTPCodecFilter::generateWindowUpdate(
  folly::IOBufQueue& buf,
  StreamID stream,
  uint32_t delta) {
  return call_->generateWindowUpdate(buf, stream, delta);
}

size_t PassThroughHTTPCodecFilter::generatePriority(
  folly::IOBufQueue& writeBuf,
  StreamID stream,
  const HTTPMessage::HTTPPriority& pri) {
  return call_->generatePriority(writeBuf, stream, pri);
}


HTTPSettings* PassThroughHTTPCodecFilter::getEgressSettings() {
  return call_->getEgressSettings();
}

const HTTPSettings* PassThroughHTTPCodecFilter::getIngressSettings() const {
  return call_->getIngressSettings();
}

void PassThroughHTTPCodecFilter::enableDoubleGoawayDrain() {
  return call_->enableDoubleGoawayDrain();
}

void PassThroughHTTPCodecFilter::setHeaderCodecStats(
    HeaderCodec::Stats* stats) {
  call_->setHeaderCodecStats(stats);
}

HTTPCodec::StreamID
PassThroughHTTPCodecFilter::getLastIncomingStreamID() const {
  return call_->getLastIncomingStreamID();
}

uint32_t PassThroughHTTPCodecFilter::getDefaultWindowSize() const {
  return call_->getDefaultWindowSize();
}

size_t
PassThroughHTTPCodecFilter::addPriorityNodes(
    PriorityQueue& queue,
    folly::IOBufQueue& writeBuf,
    uint8_t maxLevel) {
  return call_->addPriorityNodes(queue, writeBuf, maxLevel);
}

HTTPCodec::StreamID
PassThroughHTTPCodecFilter::mapPriorityToDependency(uint8_t priority) const {
  return call_->mapPriorityToDependency(priority);
}

int8_t
PassThroughHTTPCodecFilter::mapDependencyToPriority(StreamID parent) const {
  return call_->mapDependencyToPriority(parent);
}

}
