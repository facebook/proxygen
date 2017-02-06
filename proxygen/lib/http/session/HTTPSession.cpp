/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPSession.h>

#include <chrono>
#include <folly/Conv.h>
#include <wangle/acceptor/ConnectionManager.h>
#include <wangle/acceptor/SocketOptions.h>
#include <openssl/err.h>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/codec/HTTPChecks.h>
#include <proxygen/lib/http/session/HTTPSessionController.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>
#include <folly/io/async/AsyncSSLSocket.h>

using folly::AsyncSSLSocket;
using folly::AsyncSocket;
using folly::AsyncTransportWrapper;
using folly::AsyncTransport;
using folly::WriteFlags;
using folly::AsyncSocketException;
using folly::IOBuf;
using folly::SocketAddress;
using wangle::TransportInfo;
using std::pair;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

namespace {
static const uint32_t kMinReadSize = 1460;
static const uint32_t kMaxReadSize = 4000;
static const uint32_t kWriteReadyMax = 65536;

// Lower = higher latency, better prioritization
// Higher = lower latency, less prioritization
static const uint32_t kMaxWritesPerLoop = 32;

} // anonymous namespace

namespace proxygen {

uint32_t HTTPSession::kDefaultReadBufLimit = 65536;
uint32_t HTTPSession::egressBodySizeLimit_ = 4096;
uint32_t HTTPSession::kDefaultWriteBufLimit = 65536;

HTTPSession::WriteSegment::WriteSegment(
    HTTPSession* session,
    uint64_t length)
  : session_(session),
    length_(length) {
}

void
HTTPSession::WriteSegment::remove() {
  DCHECK(session_);
  DCHECK(listHook.is_linked());
  listHook.unlink();
}

void
HTTPSession::WriteSegment::detach() {
  remove();
  session_ = nullptr;
}

void
HTTPSession::WriteSegment::writeSuccess() noexcept {
  // Unlink this write segment from the list before calling
  // the session's onWriteSuccess() callback because, in the
  // case where this is the last write for the connection,
  // onWriteSuccess() looks for an empty write segment list
  // as one of the criteria for shutting down the connection.
  remove();

  // session_ should never be nullptr for a successful write
  // The session is only cleared after a write error or timeout, and all
  // AsyncTransport write failures are fatal.  If session_ is nullptr at this
  // point it means the AsyncTransport implementation is not failing
  // subsequent writes correctly after an error.
  session_->onWriteSuccess(length_);
  delete this;
}

void
HTTPSession::WriteSegment::writeErr(size_t bytesWritten,
                                    const AsyncSocketException& ex) noexcept {
  // After one segment fails to write, we clear the session_
  // pointer in all subsequent write segments, so we ignore their
  // writeError() callbacks.
  if (session_) {
    remove();
    session_->onWriteError(bytesWritten, ex);
  }
  delete this;
}

HTTPSession::HTTPSession(
  folly::HHWheelTimer* transactionTimeouts,
  AsyncTransportWrapper::UniquePtr sock,
  const SocketAddress& localAddr,
  const SocketAddress& peerAddr,
  HTTPSessionController* controller,
  unique_ptr<HTTPCodec> codec,
  const TransportInfo& tinfo,
  InfoCallback* infoCallback):
    HTTPSession(WheelTimerInstance(transactionTimeouts), std::move(sock),
        localAddr, peerAddr, controller, std::move(codec),
        tinfo, infoCallback) {
}

HTTPSession::HTTPSession(
  const WheelTimerInstance& timeout,
  AsyncTransportWrapper::UniquePtr sock,
  const SocketAddress& localAddr,
  const SocketAddress& peerAddr,
  HTTPSessionController* controller,
  unique_ptr<HTTPCodec> codec,
  const TransportInfo& tinfo,
  InfoCallback* infoCallback):
    txnEgressQueue_(isHTTP2CodecProtocol(codec->getProtocol()) ?
                    WheelTimerInstance(timeout) :
                    WheelTimerInstance()),
    localAddr_(localAddr),
    peerAddr_(peerAddr),
    sock_(std::move(sock)),
    controller_(controller),
    codec_(std::move(codec)),
    infoCallback_(infoCallback),
    writeTimeout_(this),
    flowControlTimeout_(this),
    drainTimeout_(this),
    timeout_(timeout),
    transportInfo_(tinfo),
    reads_(SocketState::PAUSED),
    writes_(SocketState::UNPAUSED),
    draining_(false),
    ingressUpgraded_(false),
    started_(false),
    writesDraining_(false),
    resetAfterDrainingWrites_(false),
    resetSocketOnShutdown_(false),
    ingressError_(false),
    inLoopCallback_(false),
    inResume_(false),
    pendingPause_(false) {

  initialReceiveWindow_ = receiveStreamWindowSize_ =
    receiveSessionWindowSize_ = codec_->getDefaultWindowSize();

  codec_.add<HTTPChecks>();

  setupCodec();

  nextEgressResults_.reserve(maxConcurrentIncomingStreams_);

  // If we receive IPv4-mapped IPv6 addresses, convert them to IPv4.
  localAddr_.tryConvertToIPv4();
  peerAddr_.tryConvertToIPv4();

  if (infoCallback_) {
    infoCallback_->onCreate(*this);
  }

  if (controller_) {
    controller_->attachSession(this);
  }

  if (!sock_->isReplaySafe()) {
    sock_->setReplaySafetyCallback(this);
  }
}

void HTTPSession::setupCodec() {
  if (!codec_->supportsParallelRequests()) {
    // until we support upstream pipelining
    maxConcurrentIncomingStreams_ = 1;
    maxConcurrentOutgoingStreamsRemote_ = isDownstream() ? 0 : 1;
  }

  HTTPSettings* settings = codec_->getEgressSettings();
  if (settings) {
    settings->setSetting(SettingsId::MAX_CONCURRENT_STREAMS,
                         maxConcurrentIncomingStreams_);
  }
  codec_->generateConnectionPreface(writeBuf_);

  if (codec_->supportsSessionFlowControl() && !connFlowControl_) {
    connFlowControl_ = new FlowControlFilter(*this, writeBuf_, codec_.call());
    codec_.addFilters(std::unique_ptr<FlowControlFilter>(connFlowControl_));
    // if we really support switching from spdy <-> h2, we need to update
    // existing flow control filter
  }

  codec_.setCallback(this);
}

HTTPSession::~HTTPSession() {
  VLOG(4) << *this << " closing";

  CHECK(transactions_.empty());
  txnEgressQueue_.dropPriorityNodes();
  CHECK(txnEgressQueue_.empty());
  DCHECK(!sock_->getReadCallback());

  if (writeTimeout_.isScheduled()) {
    writeTimeout_.cancelTimeout();
  }

  if (flowControlTimeout_.isScheduled()) {
    flowControlTimeout_.cancelTimeout();
  }

  if (infoCallback_) {
    infoCallback_->onDestroy(*this);
  }
  if (controller_) {
    controller_->detachSession(this);
    controller_ = nullptr;
  }
}

void HTTPSession::startNow() {
  CHECK(!started_);
  started_ = true;
  codec_->generateSettings(writeBuf_);
  if (connFlowControl_) {
    connFlowControl_->setReceiveWindowSize(writeBuf_,
                                           receiveSessionWindowSize_);
  }
  scheduleWrite();
  resumeReads();
}

void HTTPSession::setInfoCallback(InfoCallback* cb) {
  infoCallback_ = cb;
}

void HTTPSession::setSessionStats(HTTPSessionStats* stats) {
  sessionStats_ = stats;
  if (byteEventTracker_) {
    byteEventTracker_->setTTLBAStats(stats);
  }
}

void HTTPSession::setFlowControl(size_t initialReceiveWindow,
                                 size_t receiveStreamWindowSize,
                                 size_t receiveSessionWindowSize) {
  CHECK(!started_);
  initialReceiveWindow_ = initialReceiveWindow;
  receiveStreamWindowSize_ = receiveStreamWindowSize;
  receiveSessionWindowSize_ = receiveSessionWindowSize;
  readBufLimit_ = receiveSessionWindowSize;;
  HTTPSettings* settings = codec_->getEgressSettings();
  if (settings) {
    settings->setSetting(SettingsId::INITIAL_WINDOW_SIZE,
                         initialReceiveWindow_);
  }
}

void HTTPSession::setMaxConcurrentOutgoingStreams(uint32_t num) {
  CHECK(!started_);
  maxConcurrentOutgoingStreamsConfig_ = num;
}

void HTTPSession::setMaxConcurrentIncomingStreams(uint32_t num) {
  CHECK(!started_);
  if (codec_->supportsParallelRequests()) {
    maxConcurrentIncomingStreams_ = num;
    HTTPSettings* settings = codec_->getEgressSettings();
    if (settings) {
      settings->setSetting(SettingsId::MAX_CONCURRENT_STREAMS,
                           maxConcurrentIncomingStreams_);
    }
  }
}

void HTTPSession::setEgressBytesLimit(uint64_t bytesLimit) {
  CHECK(!started_);
  egressBytesLimit_ = bytesLimit;
}

void
HTTPSession::readTimeoutExpired() noexcept {
  VLOG(3) << "session-level timeout on " << *this;

  if (liveTransactions_ != 0) {
    // There's at least one open transaction with a read timeout scheduled.
    // We got here because the session timeout == the transaction timeout.
    // Ignore, since the transaction is going to timeout very soon.
    VLOG(4) << *this <<
        "ignoring session timeout, transaction timeout imminent";
    resetTimeout();
    return;
  }

  if (!transactions_.empty()) {
    // There are one or more transactions, but none of them are live.
    // That's valid if they've all received their full ingress messages
    // and are waiting for their Handlers to process those messages.
    VLOG(4) << *this <<
        "ignoring session timeout, no transactions awaiting reads";
    resetTimeout();
    return;
  }

  VLOG(4) << *this << " Timeout with nothing pending";

  setCloseReason(ConnectionCloseReason::TIMEOUT);
  if (controller_) {
    timeout_.scheduleTimeout(&drainTimeout_,
                             controller_->getGracefulShutdownTimeout());
  }
  notifyPendingShutdown();
}

void
HTTPSession::writeTimeoutExpired() noexcept {
  VLOG(4) << "Write timeout for " << *this;

  CHECK(!pendingWrites_.empty());
  DestructorGuard g(this);

  setCloseReason(ConnectionCloseReason::TIMEOUT);
  shutdownTransportWithReset(kErrorWriteTimeout);
}

void
HTTPSession::flowControlTimeoutExpired() noexcept {
  VLOG(4) << "Flow control timeout for " << *this;

  DestructorGuard g(this);

  setCloseReason(ConnectionCloseReason::TIMEOUT);
  shutdownTransport(true, true);
}

void
HTTPSession::describe(std::ostream& os) const {
  if (isDownstream()) {
    os << "[downstream = " << peerAddr_ << ", " << localAddr_ << " = local]";
  } else {
    os << "[local = " << localAddr_ << ", " << peerAddr_ << " = upstream]";
  }
}

bool
HTTPSession::isBusy() const {
  return !transactions_.empty() || codec_->isBusy();
}

void
HTTPSession::notifyPendingEgress() noexcept {
  scheduleWrite();
}

void
HTTPSession::notifyPendingShutdown() {
  VLOG(4) << *this << " notified pending shutdown";
  drain();
}

void
HTTPSession::closeWhenIdle() {
  // If drain() already called, this is a noop
  drain();
  // Generate the second GOAWAY now. No-op if second GOAWAY already sent.
  if (codec_->generateGoaway(writeBuf_,
                             codec_->getLastIncomingStreamID(),
                             ErrorCode::NO_ERROR)) {
    scheduleWrite();
  }
  if (!isBusy() && !hasMoreWrites()) {
    // if we're already idle, close now
    dropConnection();
  }
}

void HTTPSession::immediateShutdown() {
  if (isLoopCallbackScheduled()) {
    cancelLoopCallback();
  }
  if (shutdownTransportCb_) {
    shutdownTransportCb_.reset();
  }
  // checkForShutdown only closes the connection if these conditions are true
  DCHECK(writesShutdown());
  DCHECK(transactions_.empty());
  checkForShutdown();
}

void
HTTPSession::dropConnection() {
  VLOG(4) << "dropping " << *this;
  if (!sock_ || (readsShutdown() && writesShutdown())) {
    VLOG(4) << *this << " already shutdown";
    return;
  }

  setCloseReason(ConnectionCloseReason::SHUTDOWN);
  if (transactions_.empty() && !hasMoreWrites()) {
    DestructorGuard dg(this);
    shutdownTransport(true, true);
    // shutdownTransport might have generated a write (goaway)
    // If so, writes will not be shutdown, so fall through to
    // shutdownTransportWithReset.
    if (readsShutdown() && writesShutdown()) {
      immediateShutdown();
      return;
    }
  }
  shutdownTransportWithReset(kErrorDropped);
}

void
HTTPSession::dumpConnectionState(uint8_t loglevel) {
}

bool HTTPSession::isUpstream() const {
  return codec_->getTransportDirection() == TransportDirection::UPSTREAM;
}

bool HTTPSession::isDownstream() const {
  return codec_->getTransportDirection() == TransportDirection::DOWNSTREAM;
}

void
HTTPSession::getReadBuffer(void** buf, size_t* bufSize) {
  pair<void*,uint32_t> readSpace = readBuf_.preallocate(kMinReadSize,
                                                        kMaxReadSize);
  *buf = readSpace.first;
  *bufSize = readSpace.second;
}

void
HTTPSession::readDataAvailable(size_t readSize) noexcept {
  VLOG(10) << "read completed on " << *this << ", bytes=" << readSize;

  DestructorGuard dg(this);
  resetTimeout();
  readBuf_.postallocate(readSize);

  if (infoCallback_) {
    infoCallback_->onRead(*this, readSize);
  }

  processReadData();
}

bool
HTTPSession::isBufferMovable() noexcept {
  return true;
}

void
HTTPSession::readBufferAvailable(std::unique_ptr<IOBuf> readBuf) noexcept {
  size_t readSize = readBuf->computeChainDataLength();
  VLOG(5) << "read completed on " << *this << ", bytes=" << readSize;

  DestructorGuard dg(this);
  resetTimeout();
  readBuf_.append(std::move(readBuf));

  if (infoCallback_) {
    infoCallback_->onRead(*this, readSize);
  }

  processReadData();
}

void
HTTPSession::processReadData() {
  // skip any empty IOBufs before feeding CODEC.
  while (readBuf_.front() != nullptr && readBuf_.front()->length() == 0) {
    readBuf_.pop_front();
  }

  // Pass the ingress data through the codec to parse it. The codec
  // will invoke various methods of the HTTPSession as callbacks.
  const IOBuf* currentReadBuf;
  // It's possible for the last buffer in a chain to be empty here.
  // AsyncTransport saw fd activity so asked for a read buffer, but it was
  // SSL traffic and not enough to decrypt a whole record.  Later we invoke
  // this function from the loop callback.
  while (!ingressError_ &&
         readsUnpaused() &&
         ((currentReadBuf = readBuf_.front()) != nullptr &&
          currentReadBuf->length() != 0)) {
    // We're about to parse, make sure the parser is not paused
    codec_->setParserPaused(false);
    size_t bytesParsed = codec_->onIngress(*currentReadBuf);
    if (bytesParsed == 0) {
      // If the codec didn't make any progress with current input, we
      // better get more.
      break;
    }
    readBuf_.trimStart(bytesParsed);
  }
}

void
HTTPSession::readEOF() noexcept {
  DestructorGuard guard(this);
  VLOG(4) << "EOF on " << *this;
  // for SSL only: error without any bytes from the client might happen
  // due to client-side issues with the SSL cert. Note that it can also
  // happen if the client sends a SPDY frame header but no body.
  if (infoCallback_
      && transportInfo_.secure && transactionSeqNo_ == 0 && readBuf_.empty()) {
    infoCallback_->onIngressError(*this, kErrorClientSilent);
  }

  // Shut down reads, and also shut down writes if there are no
  // transactions.  (If there are active transactions, leave the
  // write side of the socket open so those transactions can
  // finish generating responses.)
  setCloseReason(ConnectionCloseReason::READ_EOF);
  shutdownTransport(true, transactions_.empty());
}

void
HTTPSession::readErr(const AsyncSocketException& ex) noexcept {
  DestructorGuard guard(this);
  VLOG(4) << "read error on " << *this << ": " << ex.what();

  auto sslEx = dynamic_cast<const folly::SSLException*>(&ex);
  if (infoCallback_ && sslEx) {
    if (sslEx->getSSLError() == folly::SSLError::CLIENT_RENEGOTIATION) {
      infoCallback_->onIngressError(*this, kErrorClientRenegotiation);
    }
  }

  // We're definitely finished reading. Don't close the write side
  // of the socket if there are outstanding transactions, though.
  // Instead, give the transactions a chance to produce any remaining
  // output.
  if (sslEx && sslEx->getSSLError() == folly::SSLError::SSL_ERROR) {
    transportInfo_.sslError = ex.what();
  }
  setCloseReason(ConnectionCloseReason::IO_READ_ERROR);
  shutdownTransport(true, transactions_.empty(), ex.what());
}

HTTPTransaction*
HTTPSession::newPushedTransaction(
  HTTPCodec::StreamID assocStreamId,
  HTTPTransaction::PushHandler* handler) noexcept {
  if (!codec_->supportsPushTransactions()) {
    return nullptr;
  }
  CHECK(isDownstream());
  CHECK_NOTNULL(handler);
  if (draining_ || (outgoingStreams_ >= maxConcurrentOutgoingStreamsRemote_)) {
    // This session doesn't support any more push transactions
    // This could be an actual problem - since a single downstream SPDY session
    // might be connected to N upstream hosts, each of which send M pushes,
    // which exceeds the limit.
    // should we queue?
    return nullptr;
  }

  HTTPTransaction* txn = createTransaction(codec_->createStream(),
                                           assocStreamId);
  if (!txn) {
    return nullptr;
  }

  DestructorGuard dg(this);
  auto txnID = txn->getID();
  txn->setHandler(handler);
  setNewTransactionPauseState(txnID);
  return txn;
}

size_t HTTPSession::getCodecSendWindowSize() const {
  const HTTPSettings* settings = codec_->getIngressSettings();
  if (settings) {
    return settings->getSetting(SettingsId::INITIAL_WINDOW_SIZE,
                                codec_->getDefaultWindowSize());
  }
  return codec_->getDefaultWindowSize();
}

void
HTTPSession::setNewTransactionPauseState(HTTPCodec::StreamID streamID) {
  if (!egressLimitExceeded()) {
    return;
  }

  auto txn = findTransaction(streamID);
  if (txn) {
    // If writes are paused, start this txn off in the egress paused state
    VLOG(4) << *this << " starting streamID=" << txn->getID()
            << " egress paused. pendingWriteSize_=" << pendingWriteSize_
            << ", numActiveWrites_=" << numActiveWrites_
            << ", writeBufLimit_=" << writeBufLimit_;
    txn->pauseEgress();
  }
}

http2::PriorityUpdate
HTTPSession::getMessagePriority(const HTTPMessage* msg) {
  http2::PriorityUpdate h2Pri = http2::DefaultPriority;

  // if HTTP2 priorities are enabled, get them from the message
  // and ignore otherwise
  if (getHTTP2PrioritiesEnabled() && msg) {
    auto res = msg->getHTTP2Priority();
    if (res) {
      h2Pri.streamDependency = std::get<0>(*res);
      h2Pri.exclusive = std::get<1>(*res);
      h2Pri.weight = std::get<2>(*res);
    } else {
      // HTTPMessage with setPriority called explicitly
      h2Pri.streamDependency =
        codec_->mapPriorityToDependency(msg->getPriority());
    }
  }
  return h2Pri;
}

void
HTTPSession::onMessageBegin(HTTPCodec::StreamID streamID, HTTPMessage* msg) {
  onMessageBeginImpl(streamID, 0, msg);
}

void
HTTPSession::onPushMessageBegin(HTTPCodec::StreamID streamID,
                                HTTPCodec::StreamID assocStreamID,
                                HTTPMessage* msg) {
  onMessageBeginImpl(streamID, assocStreamID, msg);
}

HTTPTransaction*
HTTPSession::onMessageBeginImpl(HTTPCodec::StreamID streamID,
                                HTTPCodec::StreamID assocStreamID,
                                HTTPMessage* msg) {
  VLOG(4) << "processing new message on " << *this << ", streamID=" << streamID;

  if (infoCallback_) {
    infoCallback_->onRequestBegin(*this);
  }
  auto txn = findTransaction(streamID);
  if (txn) {
    if (isDownstream() && txn->isPushed()) {
      // Push streams are unidirectional (half-closed). If the downstream
      // attempts to send ingress, abort with STREAM_CLOSED error.
      HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
        "Downstream attempts to send ingress, abort.");
      ex.setCodecStatusCode(ErrorCode::STREAM_CLOSED);
      txn->onError(ex);
    }
    // If this transaction is already registered, no need to add it now
    return txn;
  }

  HTTPTransaction* assocStream = nullptr;
  if (assocStreamID > 0) {
    assocStream = findTransaction(assocStreamID);
    if (!assocStream || assocStream->isIngressEOMSeen()) {
      VLOG(1) << "Can't find assoc txn=" << assocStreamID
              << ", or assoc txn cannot push";
      invalidStream(streamID, ErrorCode::PROTOCOL_ERROR);
      return nullptr;
    }
  }

  http2::PriorityUpdate messagePriority = getMessagePriority(msg);
  txn = createTransaction(streamID, assocStreamID, messagePriority);
  if (!txn) {
    // This could happen if the socket is bad.
    return nullptr;
  }

  if (assocStream && !assocStream->onPushedTransaction(txn)) {
    VLOG(1) << "Failed to add pushed transaction " << streamID << " on "
            << *this;
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
      folly::to<std::string>("Failed to add pushed transaction ", streamID));
    ex.setCodecStatusCode(ErrorCode::REFUSED_STREAM);
    onError(streamID, ex, true);
    return nullptr;
  }

  if (!codec_->supportsParallelRequests() && transactions_.size() > 1) {
    // The previous transaction hasn't completed yet. Pause reads until
    // it completes; this requires pausing both transactions.
    DCHECK_EQ(transactions_.size(), 2);
    auto prevTxn = &transactions_.begin()->second;
    if (!prevTxn->isIngressPaused()) {
      DCHECK(prevTxn->isIngressComplete());
      prevTxn->pauseIngress();
    }
    DCHECK_EQ(liveTransactions_, 1);
    txn->pauseIngress();
  }

  return txn;
}

void
HTTPSession::onHeadersComplete(HTTPCodec::StreamID streamID,
                               unique_ptr<HTTPMessage> msg) {
  // The codec's parser detected the end of an ingress message's
  // headers.
  VLOG(4) << "processing ingress headers complete for " << *this <<
      ", streamID=" << streamID;

  if (!codec_->isReusable()) {
    setCloseReason(ConnectionCloseReason::REQ_NOTREUSABLE);
  }

  if (infoCallback_) {
    infoCallback_->onIngressMessage(*this, *msg.get());
  }
  HTTPTransaction* txn = findTransaction(streamID);
  if (!txn) {
    invalidStream(streamID);
    return;
  }

  const char* sslCipher =
      transportInfo_.sslCipher ? transportInfo_.sslCipher->c_str() : nullptr;
  msg->setSecureInfo(transportInfo_.sslVersion, sslCipher);
  msg->setSecure(transportInfo_.secure);

  setupOnHeadersComplete(txn, msg.get());

  // The txn may have already been aborted by the handler.
  // Verify that the txn still exists before ingress callbacks.
  txn = findTransaction(streamID);
  if (!txn) {
    return;
  }

  if (!txn->getHandler()) {
    txn->sendAbort();
    return;
  }

  // Tell the Transaction to start processing the message now
  // that the full ingress headers have arrived.
  txn->onIngressHeadersComplete(std::move(msg));
}

void
HTTPSession::onBody(HTTPCodec::StreamID streamID,
                    unique_ptr<IOBuf> chain, uint16_t padding) {
  DestructorGuard dg(this);
  // The codec's parser detected part of the ingress message's
  // entity-body.
  uint64_t length = chain->computeChainDataLength();
  HTTPTransaction* txn = findTransaction(streamID);
  if (!txn) {
    if (connFlowControl_ &&
        connFlowControl_->ingressBytesProcessed(writeBuf_, length)) {
      scheduleWrite();
    }
    invalidStream(streamID);
    return;
  }
  auto oldSize = pendingReadSize_;
  pendingReadSize_ += length + padding;
  txn->onIngressBody(std::move(chain), padding);
  if (oldSize < pendingReadSize_) {
    // Transaction must have buffered something and not called
    // notifyBodyProcessed() on it.
    VLOG(4) << *this << " Enqueued ingress. Ingress buffer uses "
            << pendingReadSize_  << " of "  << readBufLimit_
            << " bytes.";
    if (pendingReadSize_ > readBufLimit_ &&
        oldSize <= readBufLimit_) {
      VLOG(4) << *this << " pausing due to read limit exceeded.";
      if (infoCallback_) {
        infoCallback_->onIngressLimitExceeded(*this);
      }
      pauseReads();
    }
  }
}

void HTTPSession::onChunkHeader(HTTPCodec::StreamID streamID,
                                size_t length) {
  // The codec's parser detected a chunk header (meaning that this
  // connection probably is HTTP/1.1).
  //
  // After calling onChunkHeader(), the codec will call onBody() zero
  // or more times and then call onChunkComplete().
  //
  // The reason for this callback on the chunk header is to support
  // an optimization.  In general, the job of the codec is to present
  // the HTTPSession with an abstract view of a message,
  // with all the details of wire formatting hidden.  However, there's
  // one important case where we want to know about chunking: reverse
  // proxying where both the client and server streams are HTTP/1.1.
  // In that scenario, we preserve the server's chunk boundaries when
  // sending the response to the client, in order to avoid possibly
  // making the egress packetization worse by rechunking.
  HTTPTransaction* txn = findTransaction(streamID);
  if (!txn) {
    invalidStream(streamID);
    return;
  }
  txn->onIngressChunkHeader(length);
}

void HTTPSession::onChunkComplete(HTTPCodec::StreamID streamID) {
  // The codec's parser detected the end of the message body chunk
  // associated with the most recent call to onChunkHeader().
  HTTPTransaction* txn = findTransaction(streamID);
  if (!txn) {
    invalidStream(streamID);
    return;
  }
  txn->onIngressChunkComplete();
}

void
HTTPSession::onTrailersComplete(HTTPCodec::StreamID streamID,
                                unique_ptr<HTTPHeaders> trailers) {
  HTTPTransaction* txn = findTransaction(streamID);
  if (!txn) {
    invalidStream(streamID);
    return;
  }
  txn->onIngressTrailers(std::move(trailers));
}

void
HTTPSession::onMessageComplete(HTTPCodec::StreamID streamID,
                               bool upgrade) {
  DestructorGuard dg(this);
  // The codec's parser detected the end of the ingress message for
  // this transaction.
  VLOG(4) << "processing ingress message complete for " << *this <<
      ", streamID=" << streamID;
  HTTPTransaction* txn = findTransaction(streamID);
  if (!txn) {
    invalidStream(streamID);
    return;
  }

  if (upgrade && !codec_->supportsParallelRequests()) {
    /* Send the upgrade callback to the transaction and the handler.
     * Currently we support upgrades for only HTTP sessions and not SPDY
     * sessions.
     */
    ingressUpgraded_ = true;
    txn->onIngressUpgrade(UpgradeProtocol::TCP);
    return;
  }

  // txnIngressFinished = !1xx response
  const bool txnIngressFinished =
    txn->isDownstream() || !txn->extraResponseExpected();
  if (txnIngressFinished) {
    decrementTransactionCount(txn, true, false);
  }
  txn->onIngressEOM();

  // The codec knows, based on the semantics of whatever protocol it
  // supports, whether it's valid for any more ingress messages to arrive
  // after this one.  For example, an HTTP/1.1 request containing
  // "Connection: close" indicates the end of the ingress, whereas a
  // SPDY session generally can handle more messages at any time.
  //
  // If the connection is not reusable, we close the read side of it
  // but not the write side.  There are two reasons why more writes
  // may occur after this point:
  //   * If there are previous writes buffered up in the pendingWrites_
  //     queue, we need to attempt to complete them.
  //   * The Handler associated with the transaction may want to
  //     produce more egress data when the ingress message is fully
  //     complete.  (As a common example, an application that handles
  //     form POSTs may not be able to even start generating a response
  //     until it has received the full request body.)
  //
  // There may be additional checks that need to be performed that are
  // specific to requests or responses, so we call the subclass too.
  if (!codec_->isReusable() &&
      txnIngressFinished &&
      !codec_->supportsParallelRequests()) {
    VLOG(4) << *this << " cannot reuse ingress";
    shutdownTransport(true, false);
  }
}

void HTTPSession::onError(HTTPCodec::StreamID streamID,
                          const HTTPException& error, bool newTxn) {
  DestructorGuard dg(this);
  // The codec detected an error in the ingress stream, possibly bad
  // syntax, a truncated message, or bad semantics in the frame.  If reads
  // are paused, queue up the event; otherwise, process it now.
  VLOG(4) << "Error on " << *this << ", streamID=" << streamID
          << ", " << error;

  if (ingressError_) {
    return;
  }
  if (!codec_->supportsParallelRequests()) {
    // this error should only prevent us from reading/handling more errors
    // on serial streams
    ingressError_ = true;
    setCloseReason(ConnectionCloseReason::SESSION_PARSE_ERROR);
  }
  if ((streamID == 0) && infoCallback_) {
    infoCallback_->onIngressError(*this, kErrorMessage);
  }

  if (!streamID) {
    ingressError_ = true;
    onSessionParseError(error);
    return;
  }

  HTTPTransaction* txn = findTransaction(streamID);
  if (!txn) {
    if (error.hasHttpStatusCode() && streamID != 0) {
      // If the error has an HTTP code, then parsing was fine, it just was
      // illegal in a higher level way
      txn = onMessageBeginImpl(streamID, 0, nullptr);
      if (txn) {
        handleErrorDirectly(txn, error);
      }
    } else if (newTxn) {
      onNewTransactionParseError(streamID, error);
    } else {
      VLOG(4) << *this << " parse error with invalid transaction";
      invalidStream(streamID);
    }
    return;
  }

  if (!txn->getHandler() &&
      txn->getEgressState() == HTTPTransactionEgressSM::State::Start) {
    handleErrorDirectly(txn, error);
    return;
  }

  txn->onError(error);
  if (!codec_->isReusable() && transactions_.empty()) {
    VLOG(4) << *this << "shutdown from onError";
    setCloseReason(ConnectionCloseReason::SESSION_PARSE_ERROR);
    shutdownTransport(true, true);
  }
}

void HTTPSession::onAbort(HTTPCodec::StreamID streamID,
                          ErrorCode code) {
  VLOG(4) << "stream abort on " << *this << ", streamID=" << streamID
          << ", code=" << getErrorCodeString(code);
  HTTPTransaction* txn = findTransaction(streamID);
  if (!txn) {
    VLOG(4) << *this << " abort for unrecognized transaction, streamID= "
      << streamID;
    return;
  }
  HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
    folly::to<std::string>("Stream aborted, streamID=",
      streamID, ", code=", getErrorCodeString(code)));
  ex.setProxygenError(kErrorStreamAbort);
  ex.setCodecStatusCode(code);
  DestructorGuard dg(this);
  if (isDownstream() && txn->getAssocTxnId() == 0 &&
      code == ErrorCode::CANCEL) {
    // Cancelling the assoc txn cancels all push txns
    for (auto it = txn->getPushedTransactions().begin();
         it != txn->getPushedTransactions().end(); ) {
      auto pushTxn = findTransaction(*it);
      ++it;
      DCHECK(pushTxn != nullptr);
      pushTxn->onError(ex);
    }
  }
  txn->onError(ex);
}

void HTTPSession::onGoaway(uint64_t lastGoodStreamID,
                           ErrorCode code,
                           std::unique_ptr<folly::IOBuf> debugData) {
  DestructorGuard g(this);
  VLOG(4) << "GOAWAY on " << *this << ", code=" << getErrorCodeString(code);

  setCloseReason(ConnectionCloseReason::GOAWAY);

  // Drain active transactions and prevent new transactions
  drain();

  // We give the less-forceful onGoaway() first so that transactions have
  // a chance to do stat tracking before potentially getting a forceful
  // onError().
  invokeOnAllTransactions(&HTTPTransaction::onGoaway, code);

  // Abort transactions which have been initiated but not created
  // successfully at the remote end. Upstream transactions are created
  // with odd transaction IDs and downstream transactions with even IDs.
  vector<HTTPCodec::StreamID> ids;
  HTTPCodec::StreamID firstStream = HTTPCodec::NoStream;

  for (const auto& txn: transactions_) {
    auto streamID = txn.first;
    if (((bool)(streamID & 0x01) == isUpstream()) &&
        (streamID > lastGoodStreamID)) {
      if (firstStream == HTTPCodec::NoStream) {
        // transactions_ is a set so it should be sorted by stream id.
        // We will defer adding the firstStream to the id list until
        // we can determine whether we have a codec error code.
        firstStream = streamID;
        continue;
      }

      ids.push_back(streamID);
    }
  }


  if (firstStream != HTTPCodec::NoStream && code != ErrorCode::NO_ERROR) {
    // If we get a codec error, we will attempt to blame the first stream
    // by delivering a specific error to it and let the rest of the streams
    // get a normal unacknowledged stream error.
    ProxygenError err = kErrorStreamUnacknowledged;
    string debugInfo = (debugData) ?
      folly::to<string>(" with debug info: ", (char*)debugData->data()) : "";
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
      folly::to<std::string>(getErrorString(err),
        " on transaction id: ", firstStream,
        " with codec error: ", getErrorCodeString(code),
        debugInfo));
    ex.setProxygenError(err);
    errorOnTransactionId(firstStream, std::move(ex));
  } else if (firstStream != HTTPCodec::NoStream) {
    ids.push_back(firstStream);
  }

  errorOnTransactionIds(ids, kErrorStreamUnacknowledged);
}

void HTTPSession::onPingRequest(uint64_t uniqueID) {
  VLOG(4) << *this << " got ping request with id=" << uniqueID;

  TimePoint timestamp = getCurrentTime();

  // Insert the ping reply to the head of writeBuf_
  folly::IOBufQueue pingBuf(folly::IOBufQueue::cacheChainLength());
  codec_->generatePingReply(pingBuf, uniqueID);
  size_t pingSize = pingBuf.chainLength();
  pingBuf.append(writeBuf_.move());
  writeBuf_.append(pingBuf.move());

  if (byteEventTracker_) {
    byteEventTracker_->addPingByteEvent(pingSize, timestamp, bytesScheduled_);
  }

  scheduleWrite();
}

void HTTPSession::onPingReply(uint64_t uniqueID) {
  VLOG(4) << *this << " got ping reply with id=" << uniqueID;
  if (infoCallback_) {
    infoCallback_->onPingReplyReceived();
  }
}

void HTTPSession::onWindowUpdate(HTTPCodec::StreamID streamID,
                                 uint32_t amount) {
  VLOG(4) << *this << " got window update on streamID=" << streamID << " for "
          << amount << " bytes.";
  HTTPTransaction* txn = findTransaction(streamID);
  if (!txn) {
    // We MUST be using SPDY/3+ if we got WINDOW_UPDATE. The spec says that -
    //
    // A sender should ignore all the WINDOW_UPDATE frames associated with the
    // stream after it send the last frame for the stream.
    //
    // TODO: Only ignore if this is from some past transaction
    return;
  }
  txn->onIngressWindowUpdate(amount);
}

void HTTPSession::onSettings(const SettingsList& settings) {
  DestructorGuard g(this);
  for (auto& setting: settings) {
    if (setting.id == SettingsId::INITIAL_WINDOW_SIZE) {
      onSetSendWindow(setting.value);
    } else if (setting.id == SettingsId::MAX_CONCURRENT_STREAMS) {
      onSetMaxInitiatedStreams(setting.value);
    }
  }
  if (codec_->generateSettingsAck(writeBuf_) > 0) {
    scheduleWrite();
  }
}

void HTTPSession::onSettingsAck() {
  VLOG(4) << *this << " received settings ack";
}

void HTTPSession::onPriority(HTTPCodec::StreamID streamID,
                             const HTTPMessage::HTTPPriority& pri) {
  if (!getHTTP2PrioritiesEnabled()) {
    return;
  }
  http2::PriorityUpdate h2Pri{std::get<0>(pri), std::get<1>(pri),
      std::get<2>(pri)};
  HTTPTransaction* txn = findTransaction(streamID);
  if (txn) {
    // existing txn, change pri
    txn->onPriorityUpdate(h2Pri);
  } else {
    // virtual node
    txnEgressQueue_.addOrUpdatePriorityNode(streamID, h2Pri);
  }
}

bool HTTPSession::onNativeProtocolUpgradeImpl(
  HTTPCodec::StreamID streamID, std::unique_ptr<HTTPCodec> codec,
  const std::string& protocolString) {
  CHECK_EQ(streamID, 1);
  HTTPTransaction* txn = findTransaction(streamID);
  CHECK(txn);
  // only HTTP1xCodec calls onNativeProtocolUpgrade
  CHECK(!codec_->supportsParallelRequests());

  // Reset to  defaults
  maxConcurrentIncomingStreams_ = 100;
  maxConcurrentOutgoingStreamsRemote_ = 10000;

  // overwrite destination, delay current codec deletion until the end
  // of the event loop
  auto oldCodec = codec_.setDestination(std::move(codec));
  sock_->getEventBase()->runInLoop([oldCodec = std::move(oldCodec)] () {});

  if (controller_) {
    controller_->onSessionCodecChange(this);
  }

  setupCodec();

  // txn will be streamID=1, have to make a placeholder
  (void)codec_->createStream();

  // trigger settings frame that would have gone out in startNow()
  HTTPSettings* settings = codec_->getEgressSettings();
  if (settings) {
    settings->setSetting(SettingsId::INITIAL_WINDOW_SIZE,
                         initialReceiveWindow_);
  }
  sendSettings();
  if (connFlowControl_) {
    connFlowControl_->setReceiveWindowSize(writeBuf_,
                                           receiveSessionWindowSize_);
    scheduleWrite();
  }

  // Convert the transaction that contained the Upgrade header
  txn->reset(codec_->supportsStreamFlowControl(),
             initialReceiveWindow_,
             receiveStreamWindowSize_,
             getCodecSendWindowSize());

  if (!transportInfo_.secure &&
      (!transportInfo_.appProtocol ||
       transportInfo_.appProtocol->empty())) {
    transportInfo_.appProtocol = std::make_shared<string>(
      protocolString);
  }

  return true;
}

void HTTPSession::onSetSendWindow(uint32_t windowSize) {
  VLOG(4) << *this << " got send window size adjustment. new=" << windowSize;
  invokeOnAllTransactions(&HTTPTransaction::onIngressSetSendWindow,
                          windowSize);
}

void HTTPSession::onSetMaxInitiatedStreams(uint32_t maxTxns) {
  VLOG(4) << *this << " got new maximum number of concurrent txns "
          << "we can initiate: " << maxTxns;
  const bool didSupport = supportsMoreTransactions();
  maxConcurrentOutgoingStreamsRemote_ = maxTxns;
  if (infoCallback_ && didSupport != supportsMoreTransactions()) {
    if (didSupport) {
      infoCallback_->onSettingsOutgoingStreamsFull(*this);
    } else {
      infoCallback_->onSettingsOutgoingStreamsNotFull(*this);
    }
  }
}

size_t HTTPSession::sendSettings() {
  size_t size = codec_->generateSettings(writeBuf_);
  scheduleWrite();
  return size;
}

void HTTPSession::pauseIngress(HTTPTransaction* txn) noexcept {
  VLOG(4) << *this << " pausing streamID=" << txn->getID() <<
    ", liveTransactions_ was " << liveTransactions_;
  CHECK_GT(liveTransactions_, 0);
  --liveTransactions_;
  if (liveTransactions_ == 0) {
    pauseReads();
  }
}

void HTTPSession::resumeIngress(HTTPTransaction* txn) noexcept {
  VLOG(4) << *this << " resuming streamID=" << txn->getID() <<
      ", liveTransactions_ was " << liveTransactions_;
  ++liveTransactions_;
  if (liveTransactions_ == 1) {
    resumeReads();
  }
}

void
HTTPSession::transactionTimeout(HTTPTransaction* txn) noexcept {
  // A transaction has timed out.  If the transaction does not have
  // a Handler yet, because we haven't yet received the full request
  // headers, we give it a DirectResponseHandler that generates an
  // error page.
  VLOG(3) << "Transaction timeout for streamID=" << txn->getID();
  if (!codec_->supportsParallelRequests()) {
    // this error should only prevent us from reading/handling more errors
    // on serial streams
    ingressError_ = true;
  }

  if (!txn->getHandler() &&
      txn->getEgressState() == HTTPTransactionEgressSM::State::Start) {
    VLOG(4) << *this << " Timed out receiving headers";
    if (infoCallback_) {
      infoCallback_->onIngressError(*this, kErrorTimeout);
    }
    if (codec_->supportsParallelRequests()) {
      // This can only happen with HTTP/2 where the HEADERS frame is incomplete
      // and we time out waiting for the CONTINUATION.  Abort the request.
      //
      // It would maybe be a little nicer to use the timeout handler for these
      // also.
      txn->sendAbort();
      return;
    }

    VLOG(4) << *this << " creating direct error handler";
    auto handler = getTransactionTimeoutHandler(txn);
    txn->setHandler(handler);
  }

  // Tell the transaction about the timeout.  The transaction will
  // communicate the timeout to the handler, and the handler will
  // decide how to proceed.
  txn->onIngressTimeout();
}

void HTTPSession::sendHeaders(HTTPTransaction* txn,
                              const HTTPMessage& headers,
                              HTTPHeaderSize* size,
                              bool includeEOM) noexcept {
  CHECK(started_);
  unique_ptr<IOBuf> goawayBuf;
  if (shouldShutdown()) {
    // For HTTP/1.1, add Connection: close
    // For SPDY, save the goaway for AFTER the request
    auto writeBuf = writeBuf_.move();
    drainImpl();
    goawayBuf = writeBuf_.move();
    writeBuf_.append(std::move(writeBuf));
  }
  if (isUpstream() || (txn->isPushed() && headers.isRequest())) {
    // upstream picks priority
    if (getHTTP2PrioritiesEnabled()) {
      auto pri = getMessagePriority(&headers);
      txn->onPriorityUpdate(pri);
    }
  }

  const bool wasReusable = codec_->isReusable();
  const uint64_t oldOffset = sessionByteOffset();
  // Only PUSH_PROMISE (not push response) has an associated stream
  codec_->generateHeader(writeBuf_,
                         txn->getID(),
                         headers,
                         headers.isRequest() ? txn->getAssocTxnId() : 0,
                         includeEOM,
                         size);
  const uint64_t newOffset = sessionByteOffset();

  // only do it for downstream now to bypass handling upstream reuse cases
  if (isDownstream() && headers.isResponse() &&
      newOffset > oldOffset &&
      // catch 100-ish response?
      !txn->testAndSetFirstHeaderByteSent() && byteEventTracker_) {
    byteEventTracker_->addFirstHeaderByteEvent(newOffset, txn);
  }

  if (size) {
    VLOG(4) << *this << " sending headers, size=" << size->compressed
            << ", uncompressedSize=" << size->uncompressed;
  }
  if (goawayBuf) {
    VLOG(4) << *this << " moved GOAWAY to end of writeBuf";
    writeBuf_.append(std::move(goawayBuf));
  }
  if (includeEOM) {
    commonEom(txn, 0, true);
  }
  scheduleWrite();
  onHeadersSent(headers, wasReusable);
}

void
HTTPSession::commonEom(
    HTTPTransaction* txn,
    size_t encodedSize,
    bool piggybacked) noexcept {
  // TODO: sort out the TransportCallback for all the EOM handling cases.
  //  Current code has the same behavior as before when there wasn't commonEom.
  //  The issue here is onEgressBodyLastByte can be called twice, depending on
  //  the encodedSize. E.g., when codec actually write to buffer in sendEOM.
  if (!txn->testAndSetFirstByteSent()) {
    txn->onEgressBodyFirstByte();
  }
  if (!piggybacked) {
    txn->onEgressBodyLastByte();
  }
  // in case encodedSize == 0 we won't get TTLBA which is acceptable
  // noting the fact that we don't have a response body
  if (byteEventTracker_ && (encodedSize > 0)) {
     byteEventTracker_->addLastByteEvent(
        txn,
        sessionByteOffset(),
        sock_->isEorTrackingEnabled());
  }
  onEgressMessageFinished(txn);
}

size_t
HTTPSession::sendBody(HTTPTransaction* txn,
                      std::unique_ptr<folly::IOBuf> body,
                      bool includeEOM) noexcept {
  uint64_t offset = sessionByteOffset();
  size_t bodyLen = body ? body->computeChainDataLength(): 0;
  size_t encodedSize = codec_->generateBody(writeBuf_,
                                            txn->getID(),
                                            std::move(body),
                                            HTTPCodec::NoPadding,
                                            includeEOM);
  CHECK(inLoopCallback_);
  pendingWriteSizeDelta_ -= bodyLen;
  if (encodedSize > 0 && !txn->testAndSetFirstByteSent() && byteEventTracker_) {
    byteEventTracker_->addFirstBodyByteEvent(offset, txn);
  }
  if (includeEOM) {
    VLOG(5) << *this << " sending EOM in body for streamID=" << txn->getID();
    commonEom(txn, encodedSize, true);
  }
  return encodedSize;
}

size_t HTTPSession::sendChunkHeader(HTTPTransaction* txn,
    size_t length) noexcept {
  size_t encodedSize = codec_->generateChunkHeader(writeBuf_,
                                                   txn->getID(),
                                                   length);
  scheduleWrite();
  return encodedSize;
}

size_t HTTPSession::sendChunkTerminator(
    HTTPTransaction* txn) noexcept {
  size_t encodedSize = codec_->generateChunkTerminator(writeBuf_,
                                                       txn->getID());
  scheduleWrite();
  return encodedSize;
}

size_t
HTTPSession::sendTrailers(HTTPTransaction* txn,
        const HTTPHeaders& trailers) noexcept {
  size_t encodedSize = codec_->generateTrailers(writeBuf_,
                                                txn->getID(),
                                                trailers);
  scheduleWrite();
  return encodedSize;
}

void
HTTPSession::onEgressMessageFinished(HTTPTransaction* txn, bool withRST) {
  // If the semantics of the protocol don't permit more messages
  // to be read or sent on this connection, close the socket in one or
  // more directions.
  CHECK(!transactions_.empty());

  if (infoCallback_) {
    infoCallback_->onRequestEnd(*this, txn->getMaxDeferredSize());
  }
  decrementTransactionCount(txn, false, true);
  if (withRST || ((!codec_->isReusable() || readsShutdown()) &&
                  transactions_.size() == 1)) {
    // We should shutdown reads if we are closing with RST or we aren't
    // interested in any further messages (ie if we are a downstream session).
    // Upgraded sessions have independent ingress and egress, and the reads
    // need not be shutdown on egress finish.
    if (withRST) {
      // Let any queued writes complete, but send a RST when done.
      VLOG(4) << *this << " resetting egress after this message";
      resetAfterDrainingWrites_ = true;
      setCloseReason(ConnectionCloseReason::TRANSACTION_ABORT);
      shutdownTransport(true, true);
    } else {
      // the reason is already set (either not reusable or readshutdown).

      // Defer normal shutdowns until the end of the loop.  This
      // handles an edge case with direct responses with Connection:
      // close served before ingress EOM.  The remainder of the ingress
      // message may be in the parse loop, so give it a chance to
      // finish out and avoid a kErrorEOF

      // we can get here during shutdown, in that case do not schedule a
      // shutdown callback again
      if (!shutdownTransportCb_) {
        // Just for safety, the following bumps the refcount on this session
        // to keep it live until the loopCb runs
        shutdownTransportCb_.reset(new ShutdownTransportCallback(this));
        sock_->getEventBase()->runInLoop(shutdownTransportCb_.get(), true);
      }
    }
  }
}

size_t
HTTPSession::sendEOM(HTTPTransaction* txn) noexcept {
  // Ask the codec to generate an end-of-message indicator for the
  // transaction.  Depending on the protocol, this may be a no-op.
  // Schedule a network write to send out whatever egress we might
  // have queued up.
  VLOG(4) << *this << " sending EOM for streamID=" << txn->getID();
  size_t encodedSize = codec_->generateEOM(writeBuf_, txn->getID());
  // PRIO_TODO: boost this transaction's priority? evaluate impact...
  commonEom(txn, encodedSize, false);
  return encodedSize;
}

size_t HTTPSession::sendAbort(HTTPTransaction* txn,
                              ErrorCode statusCode) noexcept {
  // Ask the codec to generate an abort indicator for the transaction.
  // Depending on the protocol, this may be a no-op.
  // Schedule a network write to send out whatever egress we might
  // have queued up.
  VLOG(4) << *this << " sending abort for streamID=" << txn->getID();
  // drain this transaction's writeBuf instead of flushing it
  // then enqueue the abort directly into the Session buffer,
  // hence with max priority.
  size_t encodedSize = codec_->generateRstStream(writeBuf_,
                                                 txn->getID(),
                                                 statusCode);

  if (!codec_->isReusable()) {
    // HTTP 1x codec does not support per stream abort so this will
    // render the codec not reusable
    setCloseReason(ConnectionCloseReason::TRANSACTION_ABORT);
  }

  scheduleWrite();

  // If the codec wasn't able to write a L7 message for the abort, then
  // fall back to closing the transport with a TCP level RST
  onEgressMessageFinished(txn, !encodedSize);
  return encodedSize;
}

size_t HTTPSession::sendPriority(HTTPTransaction* txn,
                                 const http2::PriorityUpdate& pri) noexcept {
  return sendPriorityImpl(txn->getID(), pri);
}

void
HTTPSession::decrementTransactionCount(HTTPTransaction* txn,
                                       bool ingressEOM, bool egressEOM) {
  if ((isUpstream() && !txn->isPushed()) ||
      (isDownstream() && txn->isPushed())) {
    if (ingressEOM && txn->testAndClearActive()) {
      outgoingStreams_--;
    }
  } else {
    if (egressEOM && txn->testAndClearActive()) {
      incomingStreams_--;
    }
  }
}

void
HTTPSession::detach(HTTPTransaction* txn) noexcept {
  DestructorGuard guard(this);
  HTTPCodec::StreamID streamID = txn->getID();
  auto it = transactions_.find(txn->getID());
  DCHECK(it != transactions_.end());

  if (txn->isIngressPaused()) {
    // Someone detached a transaction that was paused.  Make the resumeIngress
    // call to keep liveTransactions_ in order
    VLOG(4) << *this << " detached paused transaction=" << streamID;
    resumeIngress(txn);
  }

  VLOG(4) << *this << " removing streamID=" << streamID <<
    ", liveTransactions was " << liveTransactions_;
  CHECK_GT(liveTransactions_, 0);
  liveTransactions_--;

  if (txn->isPushed()) {
    auto assocTxn = findTransaction(txn->getAssocTxnId());
    if (assocTxn) {
      assocTxn->removePushedTransaction(streamID);
    }
  }
  decrementTransactionCount(txn, true, true);
  transactions_.erase(it);

  if (transactions_.empty()) {
    latestActive_ = getCurrentTime();
    if (infoCallback_) {
      infoCallback_->onDeactivateConnection(*this);
    }
    if (getConnectionManager()) {
      getConnectionManager()->onDeactivated(*this);
    }
  } else {
    if (infoCallback_) {
      infoCallback_->onTransactionDetached(*this);
    }
  }

  if (!readsShutdown()) {
    if (!codec_->supportsParallelRequests() && !transactions_.empty()) {
      // If we had more than one transaction, then someone tried to pipeline and
      // we paused reads
      DCHECK_EQ(transactions_.size(), 1);
      auto& nextTxn = transactions_.begin()->second;
      DCHECK(nextTxn.isIngressPaused());
      DCHECK(!nextTxn.isIngressComplete());
      nextTxn.resumeIngress();
      return;
    } else {
      // this will resume reads if they were paused (eg: 0 HTTP transactions)
      resumeReads();
    }
  }

  if (liveTransactions_ == 0 && transactions_.empty() && !isScheduled()) {
    resetTimeout();
  }

  // It's possible that this is the last transaction in the session,
  // so check whether the conditions for shutdown are satisfied.
  if (transactions_.empty()) {
    if (shouldShutdown()) {
      writesDraining_ = true;
    }
    // Handle the case where we are draining writes but all remaining
    // transactions terminated with no egress.
    if (writesDraining_ && !writesShutdown() && !hasMoreWrites()) {
      shutdownTransport(false, true);
      return;
    }
  }
  checkForShutdown();
}

size_t
HTTPSession::sendWindowUpdate(HTTPTransaction* txn,
                              uint32_t bytes) noexcept {
  size_t sent = codec_->generateWindowUpdate(writeBuf_, txn->getID(), bytes);
  if (sent) {
    scheduleWrite();
  }
  return sent;
}

void
HTTPSession::notifyIngressBodyProcessed(uint32_t bytes) noexcept {
  CHECK_GE(pendingReadSize_, bytes);
  auto oldSize = pendingReadSize_;
  pendingReadSize_ -= bytes;
  VLOG(4) << *this << " Dequeued " << bytes << " bytes of ingress. "
    << "Ingress buffer uses " << pendingReadSize_  << " of "
    << readBufLimit_ << " bytes.";
  if (connFlowControl_ &&
      connFlowControl_->ingressBytesProcessed(writeBuf_, bytes)) {
    scheduleWrite();
  }
  if (oldSize > readBufLimit_ &&
      pendingReadSize_ <= readBufLimit_) {
    resumeReads();
  }
}

void
HTTPSession::notifyEgressBodyBuffered(int64_t bytes) noexcept {
  pendingWriteSizeDelta_ += bytes;
  // any net change requires us to update pause/resume state in the
  // loop callback
  if (pendingWriteSizeDelta_ > 0) {
    // pause inline, resume in loop
    updateWriteBufSize(0);
  } else if (!isLoopCallbackScheduled()) {
    sock_->getEventBase()->runInLoop(this);
  }
}

const SocketAddress& HTTPSession::getLocalAddress() const noexcept {
  return localAddr_;
}

const SocketAddress& HTTPSession::getPeerAddress() const noexcept {
  return peerAddr_;
}

TransportInfo& HTTPSession::getSetupTransportInfo() noexcept {
  return transportInfo_;
}

const TransportInfo& HTTPSession::getSetupTransportInfo() const noexcept {
  return transportInfo_;
}

bool HTTPSession::getCurrentTransportInfoWithoutUpdate(
    TransportInfo* tinfo) const {
  auto sock = sock_->getUnderlyingTransport<AsyncSocket>();
  if (sock) {
    tinfo->initWithSocket(sock);
    return true;
  }
  return false;
}

bool HTTPSession::getCurrentTransportInfo(TransportInfo* tinfo) {
  if (getCurrentTransportInfoWithoutUpdate(tinfo)) {
    // some fields are the same with the setup transport info
    tinfo->setupTime = transportInfo_.setupTime;
    tinfo->secure = transportInfo_.secure;
    tinfo->sslSetupTime = transportInfo_.sslSetupTime;
    tinfo->sslVersion = transportInfo_.sslVersion;
    tinfo->sslCipher = transportInfo_.sslCipher;
    tinfo->sslResume = transportInfo_.sslResume;
    tinfo->appProtocol = transportInfo_.appProtocol;
    tinfo->sslError = transportInfo_.sslError;
#if defined(__linux__) || defined(__FreeBSD__)
    // update connection transport info with the latest RTT
    if (tinfo->tcpinfo.tcpi_rtt > 0) {
      transportInfo_.tcpinfo.tcpi_rtt = tinfo->tcpinfo.tcpi_rtt;
      transportInfo_.rtt = std::chrono::microseconds(tinfo->tcpinfo.tcpi_rtt);
    }
    transportInfo_.rtx = tinfo->rtx;
#endif
    return true;
  }
  return false;
}

void HTTPSession::setByteEventTracker(
    std::shared_ptr<ByteEventTracker> byteEventTracker) {
  if (byteEventTracker && byteEventTracker_) {
    byteEventTracker->absorb(std::move(*byteEventTracker_));
  }
  byteEventTracker_ = byteEventTracker;
  if (byteEventTracker_) {
    byteEventTracker_->setCallback(this);
    byteEventTracker_->setTTLBAStats(sessionStats_);
  }
}

unique_ptr<IOBuf> HTTPSession::getNextToSend(bool* cork, bool* eom) {
  // limit ourselves to one outstanding write at a time (onWriteSuccess calls
  // scheduleWrite)
  if (numActiveWrites_ > 0 || writesShutdown()) {
    VLOG(4) << "skipping write during this loop, numActiveWrites_=" <<
      numActiveWrites_ << " writesShutdown()=" << writesShutdown();
    return nullptr;
  }

  // We always tack on at least one body packet to the current write buf
  // This ensures that a short HTTPS response will go out in a single SSL record
  while (!txnEgressQueue_.empty()) {
    uint32_t toSend = kWriteReadyMax;
    if (connFlowControl_) {
      if (connFlowControl_->getAvailableSend() == 0) {
        VLOG(4) << "Session-level send window is full, skipping remaining "
                << "body writes this loop";
        break;
      }
      toSend = std::min(toSend, connFlowControl_->getAvailableSend());
    }
    txnEgressQueue_.nextEgress(nextEgressResults_,
                               isSpdyCodecProtocol(codec_->getProtocol()));
    CHECK(!nextEgressResults_.empty()); // Queue was non empty, so this must be
    // The maximum we will send for any transaction in this loop
    uint32_t txnMaxToSend = toSend * nextEgressResults_.front().second;
    if (txnMaxToSend == 0) {
      // toSend is smaller than the number of transactions.  Give all egress
      // to the first transaction
      nextEgressResults_.erase(++nextEgressResults_.begin(),
                               nextEgressResults_.end());
      txnMaxToSend = std::min(toSend, egressBodySizeLimit_);
      nextEgressResults_.front().second = 1;
    }
    if (nextEgressResults_.size() > 1 && txnMaxToSend > egressBodySizeLimit_) {
      // Cap the max to egressBodySizeLimit_, and recompute toSend accordingly
      txnMaxToSend = egressBodySizeLimit_;
      toSend = txnMaxToSend / nextEgressResults_.front().second;
    }
    // split allowed by relative weight, with some minimum
    for (auto txnPair: nextEgressResults_) {
      uint32_t txnAllowed = txnPair.second * toSend;
      if (nextEgressResults_.size() > 1) {
        CHECK_LE(txnAllowed, egressBodySizeLimit_);
      }
      if (connFlowControl_) {
        CHECK_LE(txnAllowed, connFlowControl_->getAvailableSend());
      }
      if (txnAllowed == 0) {
        // The ratio * toSend was so small this txn gets nothing.
        VLOG(4) << *this << " breaking egress loop on 0 txnAllowed";
        break;
      }

      VLOG(4) << *this << " egressing txnID=" << txnPair.first->getID() <<
        " allowed=" << txnAllowed;
      txnPair.first->onWriteReady(txnAllowed, txnPair.second);
    }
    nextEgressResults_.clear();
    // it can be empty because of HTTPTransaction rate limiting.  We should
    // change rate limiting to clearPendingEgress while waiting.
    if (!writeBuf_.empty()) {
      break;
    }
  }
  *eom = false;
  if (byteEventTracker_) {
    uint64_t needed = byteEventTracker_->preSend(cork, eom, bytesWritten_);
    if (needed > 0) {
      VLOG(5) << *this << " writeBuf_.chainLength(): "
              << writeBuf_.chainLength() << " txnEgressQueue_.empty(): "
              << txnEgressQueue_.empty();

      if (needed < writeBuf_.chainLength()) {
        // split the next EOM chunk
        VLOG(5) << *this << " splitting " << needed << " bytes out of a "
                << writeBuf_.chainLength() << " bytes IOBuf";
        *cork = true;
        if (sessionStats_) {
          sessionStats_->recordTTLBAIOBSplitByEom();
        }
        return writeBuf_.split(needed);
      } else {
        CHECK_EQ(needed, writeBuf_.chainLength());
      }
    }
  }

  // cork if there are txns with pending egress and room to send them
  *cork = !txnEgressQueue_.empty() && !isConnWindowFull();
  return writeBuf_.move();
}

void
HTTPSession::runLoopCallback() noexcept {
  // We schedule this callback to run at the end of an event
  // loop iteration if either of two conditions has happened:
  //   * The session has generated some egress data (see scheduleWrite())
  //   * Reads have become unpaused (see resumeReads())
  DestructorGuard dg(this);
  inLoopCallback_ = true;
  folly::ScopeGuard scopeg = folly::makeGuard([this] {
      inLoopCallback_ = false;
      // This ScopeGuard needs to be under the above DestructorGuard
      if (pendingWriteSizeDelta_) {
        updateWriteBufSize(0);
      }
      checkForShutdown();
    });
  VLOG(5) << *this << " in loop callback";

  for (uint32_t i = 0; i < kMaxWritesPerLoop; ++i) {
    bool cork = true;
    bool eom = false;
    unique_ptr<IOBuf> writeBuf = getNextToSend(&cork, &eom);

    if (!writeBuf) {
      break;
    }
    uint64_t len = writeBuf->computeChainDataLength();
    VLOG(11) << *this
             << " bytes of egress to be written: " << len
             << " cork:" << cork << " eom:" << eom;
    if (len == 0) {
      checkForShutdown();
      return;
    }

    WriteSegment* segment = new WriteSegment(this, len);
    segment->setCork(cork);
    segment->setEOR(eom);

    pendingWrites_.push_back(*segment);
    if (!writeTimeout_.isScheduled()) {
      // Any performance concern here?
      timeout_.scheduleTimeout(&writeTimeout_);
    }
    numActiveWrites_++;
    VLOG(4) << *this << " writing " << len << ", activeWrites="
             << numActiveWrites_ << " cork=" << cork << " eom=" << eom;
    bytesScheduled_ += len;
    sock_->writeChain(segment, std::move(writeBuf), segment->getFlags());
    if (numActiveWrites_ > 0) {
      updateWriteCount();
      pendingWriteSizeDelta_ += len;
      // updateWriteBufSize called in scope guard
      break;
    }
    // writeChain can result in a writeError and trigger the shutdown code path
  }
  if (numActiveWrites_ == 0 && !writesShutdown() && hasMoreWrites() &&
      (!connFlowControl_ || connFlowControl_->getAvailableSend())) {
    scheduleWrite();
  }

  if (readsUnpaused()) {
    processReadData();

    // Install the read callback if necessary
    if (readsUnpaused() && !sock_->getReadCallback()) {
      sock_->setReadCB(this);
    }
  }
  // checkForShutdown is now in ScopeGuard
}

void
HTTPSession::scheduleWrite() {
  // Do all the network writes for this connection in one batch at
  // the end of the current event loop iteration.  Writing in a
  // batch helps us packetize the network traffic more efficiently,
  // as well as saving a few system calls.
  if (!isLoopCallbackScheduled() &&
      (writeBuf_.front() || !txnEgressQueue_.empty())) {
    VLOG(5) << *this << " scheduling write callback";
    sock_->getEventBase()->runInLoop(this);
  }
}

bool HTTPSession::egressLimitExceeded() const {
  return pendingWriteSize_ >= writeBufLimit_;
}

void
HTTPSession::updateWriteCount() {
  if (numActiveWrites_ > 0 && writesUnpaused()) {
    // Exceeded limit. Pause reading on the incoming stream.
    VLOG(3) << "Pausing egress for " << *this;
    writes_ = SocketState::PAUSED;
  } else if (numActiveWrites_ == 0 && writesPaused()) {
    // Dropped below limit. Resume reading on the incoming stream if needed.
    VLOG(3) << "Resuming egress for " << *this;
    writes_ = SocketState::UNPAUSED;
  }
}

void
HTTPSession::updateWriteBufSize(int64_t delta) {
  // This is the sum of body bytes buffered within transactions_ and in
  // the sock_'s write buffer.
  delta += pendingWriteSizeDelta_;
  pendingWriteSizeDelta_ = 0;
  DCHECK(delta >= 0 || uint64_t(-delta) <= pendingWriteSize_);
  bool wasExceeded = egressLimitExceeded();
  pendingWriteSize_ += delta;

  if (egressLimitExceeded() && !wasExceeded) {
    // Exceeded limit. Pause reading on the incoming stream.
    if (inResume_) {
      VLOG(3) << "Pausing txn egress for " << *this << " deferred";
      pendingPause_ = true;
    } else {
      VLOG(3) << "Pausing txn egress for " << *this;
      invokeOnAllTransactions(&HTTPTransaction::pauseEgress);
    }
  } else if (!egressLimitExceeded() && wasExceeded) {
    // Dropped below limit. Resume reading on the incoming stream if needed.
    if (inResume_) {
      if (pendingPause_) {
        VLOG(3) << "Cancel deferred txn egress pause for " << *this;
        pendingPause_ = false;
      } else {
        VLOG(3) << "Ignoring redundant resume for " << *this;
      }
    } else {
      VLOG(3) << "Resuming txn egress for " << *this;
      resumeTransactions();
    }
  }
}

void
HTTPSession::shutdownTransport(bool shutdownReads,
                               bool shutdownWrites,
                               const std::string& errorMsg) {
  DestructorGuard guard(this);

  // shutdowns not accounted for, shouldn't see any
  setCloseReason(ConnectionCloseReason::UNKNOWN);

  VLOG(4) << "shutdown request for " << *this << ": reads="
          << shutdownReads << " (currently " << readsShutdown()
          << "), writes=" << shutdownWrites << " (currently "
          << writesShutdown() << ")";

  bool notifyEgressShutdown = false;
  bool notifyIngressShutdown = false;

  ProxygenError error;
  if (!transportInfo_.sslError.empty()) {
    error = kErrorSSL;
  } else if (sock_->error()) {
    VLOG(3) << "shutdown request for " << *this
      << " on bad socket. Shutting down writes too.";
    if (closeReason_ == ConnectionCloseReason::IO_WRITE_ERROR) {
      error = kErrorWrite;
    } else {
      error = kErrorConnectionReset;
    }
    shutdownWrites = true;
  } else if (closeReason_ == ConnectionCloseReason::TIMEOUT) {
    error = kErrorTimeout;
  } else {
    error = kErrorEOF;
  }

  if (shutdownReads && !shutdownWrites && flowControlTimeout_.isScheduled()) {
    // reads are dead and writes are blocked on a window update that will never
    // come.  shutdown writes too.
    VLOG(4) << *this << " Converting read shutdown to read/write due to"
      " flow control";
    shutdownWrites = true;
  }

  if (shutdownWrites && !writesShutdown()) {
    if (codec_->generateGoaway(writeBuf_,
                               codec_->getLastIncomingStreamID(),
                               ErrorCode::NO_ERROR)) {
      scheduleWrite();
    }
    if (!hasMoreWrites() &&
        (transactions_.empty() || codec_->closeOnEgressComplete())) {
      writes_ = SocketState::SHUTDOWN;
      if (byteEventTracker_) {
        byteEventTracker_->drainByteEvents();
      }
      if (resetAfterDrainingWrites_) {
        VLOG(4) << *this << " writes drained, sending RST";
        resetSocketOnShutdown_ = true;
        shutdownReads = true;
      } else {
        VLOG(4) << *this << " writes drained, closing";
        sock_->shutdownWriteNow();
      }
      notifyEgressShutdown = true;
    } else if (!writesDraining_) {
      writesDraining_ = true;
      notifyEgressShutdown = true;
    } // else writes are already draining; don't double notify
  }

  if (shutdownReads && !readsShutdown()) {
    notifyIngressShutdown = true;
    // TODO: send an RST if readBuf_ is non empty?
    sock_->setReadCB(nullptr);
    reads_ = SocketState::SHUTDOWN;
    if (!transactions_.empty() && error == kErrorConnectionReset) {
      if (infoCallback_ != nullptr) {
        infoCallback_->onIngressError(*this, error);
      }
    } else if (error == kErrorEOF) {
      // Report to the codec that the ingress stream has ended
      codec_->onIngressEOF();
      if (infoCallback_) {
        infoCallback_->onIngressEOF();
      }
    }
    // Once reads are shutdown the parser should stop processing
    codec_->setParserPaused(true);
  }

  if (notifyIngressShutdown || notifyEgressShutdown) {
    auto dir = (notifyIngressShutdown && notifyEgressShutdown) ?
      HTTPException::Direction::INGRESS_AND_EGRESS :
      (notifyIngressShutdown ? HTTPException::Direction::INGRESS :
         HTTPException::Direction::EGRESS);
    HTTPException ex(dir, folly::to<std::string>(
        "Shutdown transport: ", getErrorString(error),
        errorMsg.empty() ? "" : " ", errorMsg));
    ex.setProxygenError(error);
    invokeOnAllTransactions(&HTTPTransaction::onError, ex);
  }

  // Close the socket only after the onError() callback on the txns
  // and handler has been detached.
  checkForShutdown();
}

void HTTPSession::shutdownTransportWithReset(
    ProxygenError errorCode,
    const std::string& errorMsg) {
  DestructorGuard guard(this);
  VLOG(4) << "shutdownTransportWithReset";

  if (!readsShutdown()) {
    sock_->setReadCB(nullptr);
    reads_ = SocketState::SHUTDOWN;
  }

  if (!writesShutdown()) {
    writes_ = SocketState::SHUTDOWN;
    IOBuf::destroy(writeBuf_.move());
    while (!pendingWrites_.empty()) {
      pendingWrites_.front().detach();
      numActiveWrites_--;
    }
    VLOG(4) << *this << " cancel write timer";
    writeTimeout_.cancelTimeout();
    resetSocketOnShutdown_ = true;
  }

  errorOnAllTransactions(errorCode, errorMsg);
  // drainByteEvents() can call detach(txn), which can in turn call
  // shutdownTransport if we were already draining. To prevent double
  // calling onError() to the transactions, we call drainByteEvents()
  // after we've given the explicit error.
  if (byteEventTracker_) {
    byteEventTracker_->drainByteEvents();
  }

  // HTTPTransaction::onError could theoretically schedule more callbacks,
  // so do this last.
  if (isLoopCallbackScheduled()) {
    cancelLoopCallback();
  }
  // onError() callbacks or drainByteEvents() could result in txns detaching
  // due to CallbackGuards going out of scope. Close the socket only after
  // the txns are detached.
  checkForShutdown();
}

void
HTTPSession::checkForShutdown() {
  VLOG(10) << *this << " checking for shutdown, readShutdown="
           << readsShutdown() << ", writesShutdown=" << writesShutdown()
           << ", transaction set empty=" << transactions_.empty();

  // Two conditions are required to destroy the HTTPSession:
  //   * All writes have been finished.
  //   * There are no transactions remaining on the session.
  if (writesShutdown() && transactions_.empty() &&
      !isLoopCallbackScheduled()) {
    VLOG(4) << "destroying " << *this;
    sock_->setReadCB(nullptr);
    auto asyncSocket = sock_->getUnderlyingTransport<folly::AsyncSocket>();
    if (asyncSocket) {
      asyncSocket->setBufferCallback(nullptr);
    }
    reads_ = SocketState::SHUTDOWN;
    if (resetSocketOnShutdown_) {
      sock_->closeWithReset();
    } else {
      sock_->closeNow();
    }
    destroy();
  }
}

void
HTTPSession::drain() {
  if (!draining_) {
    VLOG(4) << *this << " draining";
    draining_ = true;
    setCloseReason(ConnectionCloseReason::SHUTDOWN);

    if (allTransactionsStarted()) {
      drainImpl();
    }
    if (transactions_.empty() && isUpstream()) {
      // We don't do this for downstream since we need to wait for
      // inflight requests to arrive
      VLOG(4) << *this << " shutdown from drain";
      shutdownTransport(true, true);
    }
  } else {
    VLOG(4) << *this << " already draining";
  }
}

void HTTPSession::drainImpl() {
  if (codec_->isReusable() || codec_->isWaitingToDrain()) {
    setCloseReason(ConnectionCloseReason::SHUTDOWN);
    codec_->generateGoaway(writeBuf_,
                           getGracefulGoawayAck(),
                           ErrorCode::NO_ERROR);
    scheduleWrite();
  }
}

bool HTTPSession::shouldShutdown() const {
  return draining_ &&
    allTransactionsStarted() &&
    (!codec_->supportsParallelRequests() ||
     isUpstream() ||
     !codec_->isReusable());
}

size_t HTTPSession::sendPing() {
  const size_t bytes = codec_->generatePingRequest(writeBuf_);
  if (bytes) {
    scheduleWrite();
  }
  return bytes;
}

HTTPCodec::StreamID HTTPSession::sendPriority(http2::PriorityUpdate pri) {
  if (!codec_->supportsParallelRequests()) {
    // For HTTP/1.1, don't call createStream()
    return 0;
  }
  auto id = codec_->createStream();
  sendPriority(id, pri);
  return id;
}

size_t HTTPSession::sendPriority(HTTPCodec::StreamID id,
                                 http2::PriorityUpdate pri) {
  auto res = sendPriorityImpl(id, pri);
  txnEgressQueue_.addOrUpdatePriorityNode(id, pri);
  return res;
}


size_t HTTPSession::sendPriorityImpl(HTTPCodec::StreamID id,
                                     http2::PriorityUpdate pri) {
  CHECK_NE(id, 0);
  const size_t bytes = codec_->generatePriority(
    writeBuf_, id, std::make_tuple(pri.streamDependency,
                                   pri.exclusive,
                                   pri.weight));
  if (bytes) {
    scheduleWrite();
  }
  return bytes;
}

HTTPTransaction*
HTTPSession::findTransaction(HTTPCodec::StreamID streamID) {
  auto it = transactions_.find(streamID);
  if (it == transactions_.end()) {
    return nullptr;
  } else {
    return &it->second;
  }
}

HTTPTransaction*
HTTPSession::createTransaction(HTTPCodec::StreamID streamID,
                               HTTPCodec::StreamID assocStreamID,
                               http2::PriorityUpdate priority) {
  if (!sock_->good() || transactions_.count(streamID)) {
    // Refuse to add a transaction on a closing session or if a
    // transaction of that ID already exists.
    return nullptr;
  }

  if (transactions_.empty()) {
    if (infoCallback_) {
      infoCallback_->onActivateConnection(*this);
    }
    if (getConnectionManager()) {
      getConnectionManager()->onActivated(*this);
    }
    if (numTxnServed_ >= 1) {
      // idle duration only exists since the 2nd transaction in the session
      latestIdleDuration_ = secondsSince(latestActive_);
    }
  }

  auto matchPair = transactions_.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(streamID),
    std::forward_as_tuple(
      codec_->getTransportDirection(), streamID, transactionSeqNo_, *this,
      txnEgressQueue_, timeout_, sessionStats_,
      codec_->supportsStreamFlowControl(),
      initialReceiveWindow_,
      getCodecSendWindowSize(),
      priority, assocStreamID));

  CHECK(matchPair.second) << "Emplacement failed, despite earlier "
    "existence check.";

  HTTPTransaction* txn = &matchPair.first->second;

  if (numTxnServed_ > 0) {
    auto stats = txn->getSessionStats();
    if (stats != nullptr) {
      stats->recordSessionReused();
    }
  }
  ++numTxnServed_;

  VLOG(5) << *this << " adding streamID=" << txn->getID()
          << ", liveTransactions_ was " << liveTransactions_;

  ++liveTransactions_;
  ++transactionSeqNo_;
  txn->setReceiveWindow(receiveStreamWindowSize_);

  if ((isUpstream() && !txn->isPushed()) ||
      (isDownstream() && txn->isPushed())) {
    outgoingStreams_++;
    if (outgoingStreams_ > historicalMaxOutgoingStreams_) {
      historicalMaxOutgoingStreams_ = outgoingStreams_;
    }
  } else {
    incomingStreams_++;
  }

  return txn;
}

void
HTTPSession::onWriteSuccess(uint64_t bytesWritten) {
  DestructorGuard dg(this);
  bytesWritten_ += bytesWritten;
  transportInfo_.totalBytes += bytesWritten;
  CHECK(writeTimeout_.isScheduled());
  if (pendingWrites_.empty()) {
    VLOG(10) << "Cancel write timer on last successful write";
    writeTimeout_.cancelTimeout();
  } else {
    VLOG(10) << "Refresh write timer on writeSuccess";
    timeout_.scheduleTimeout(&writeTimeout_);
  }

  if (infoCallback_) {
    infoCallback_->onWrite(*this, bytesWritten);
  }

  VLOG(5) << "total bytesWritten_: " << bytesWritten_;

  // processByteEvents will return true if it has been replaced with another
  // tracker in the middle and needs to be re-run.  Should happen at most
  // once.  while with no body is intentional
  while (byteEventTracker_ &&
         byteEventTracker_->processByteEvents(
           byteEventTracker_, bytesWritten_,
           sock_->isEorTrackingEnabled())) {} // pass

  if ((!codec_->isReusable() || readsShutdown()) && (transactions_.empty())) {
    if (!codec_->isReusable()) {
      // Shouldn't happen unless there is a bug. This can only happen when
      // someone calls shutdownTransport, but did not specify a reason before.
      setCloseReason(ConnectionCloseReason::UNKNOWN);
    }
    VLOG(4) << *this << " shutdown from onWriteSuccess";
    shutdownTransport(true, true);
  }
  numActiveWrites_--;
  if (!inLoopCallback_) {
    updateWriteCount();
    // safe to resume here:
    updateWriteBufSize(-folly::to<int64_t>(bytesWritten));
    // PRIO_FIXME: this is done because of the corking business...
    //             in the future we may want to have a pull model
    //             whereby the socket asks us for a given amount of
    //             data to send...
    if (numActiveWrites_ == 0 && hasMoreWrites()) {
      runLoopCallback();
    }
  }
  onWriteCompleted();

  if (egressBytesLimit_ > 0 && bytesWritten_ >= egressBytesLimit_) {
    VLOG(4) << "Egress limit reached, shutting down "
      "session (egressed " << bytesWritten_ << ", limit set to "
      << egressBytesLimit_ << ")";
    shutdownTransport(true, true);
  }
}

void
HTTPSession::onWriteError(size_t bytesWritten,
                          const AsyncSocketException& ex) {
  VLOG(4) << *this << " write error: " << ex.what();
  if (infoCallback_) {
    infoCallback_->onWrite(*this, bytesWritten);
  }

  auto sslEx = dynamic_cast<const folly::SSLException*>(&ex);
  // Save the SSL error, if there was one.  It will be recorded later
  if (sslEx && sslEx->getSSLError() == folly::SSLError::SSL_ERROR) {
    transportInfo_.sslError = ex.what();
  }

  setCloseReason(ConnectionCloseReason::IO_WRITE_ERROR);
  shutdownTransportWithReset(kErrorWrite, ex.what());
}

void
HTTPSession::onWriteCompleted() {
  if (!writesDraining_) {
    return;
  }

  if (numActiveWrites_) {
    return;
  }

  // Don't shutdown if there might be more writes
  if (!pendingWrites_.empty()) {
    return;
  }

  // All finished draining writes, so shut down the egress
  shutdownTransport(false, true);
}

void HTTPSession::onSessionParseError(const HTTPException& error) {
  VLOG(4) << *this << " session layer parse error. Terminate the session.";
  if (error.hasCodecStatusCode()) {
    std::unique_ptr<folly::IOBuf> errorMsg =
      folly::IOBuf::copyBuffer(error.what());
    codec_->generateGoaway(writeBuf_,
                           codec_->getLastIncomingStreamID(),
                           error.getCodecStatusCode(),
                           isHTTP2CodecProtocol(codec_->getProtocol()) ?
                           std::move(errorMsg) : nullptr);
    scheduleWrite();
  }
  setCloseReason(ConnectionCloseReason::SESSION_PARSE_ERROR);
  shutdownTransport(true, true);
}

void HTTPSession::onNewTransactionParseError(HTTPCodec::StreamID streamID,
                                             const HTTPException& error) {
  VLOG(4) << *this << " parse error with new transaction";
  if (error.hasCodecStatusCode()) {
    codec_->generateRstStream(writeBuf_, streamID, error.getCodecStatusCode());
    scheduleWrite();
  }
  if (!codec_->isReusable()) {
    // HTTP 1x codec does not support per stream abort so this will
    // render the codec not reusable
    setCloseReason(ConnectionCloseReason::SESSION_PARSE_ERROR);
  }
}

void
HTTPSession::handleErrorDirectly(HTTPTransaction* txn,
                                 const HTTPException& error) {
  VLOG(4) << *this << " creating direct error handler";
  DCHECK(txn);
  auto handler = getParseErrorHandler(txn, error);
  if (!handler) {
    txn->sendAbort();
    return;
  }
  txn->setHandler(handler);
  if (infoCallback_) {
    infoCallback_->onIngressError(*this, error.getProxygenError());
  }
  txn->onError(error);
}

void
HTTPSession::pauseReads() {
  // Make sure the parser is paused.  Note that if reads are shutdown
  // before they are paused, we never make it past the if.
  codec_->setParserPaused(true);
  if (!readsUnpaused() ||
      (codec_->supportsParallelRequests() &&
       pendingReadSize_ <= readBufLimit_)) {
    return;
  }
  pauseReadsImpl();
}

void HTTPSession::pauseReadsImpl() {
  VLOG(4) << *this << ": pausing reads";
  if (infoCallback_) {
    infoCallback_->onIngressPaused(*this);
  }
  cancelTimeout();
  sock_->setReadCB(nullptr);
  reads_ = SocketState::PAUSED;
}

void
HTTPSession::resumeReads() {
  if (!readsPaused() ||
      (codec_->supportsParallelRequests() &&
       pendingReadSize_ > readBufLimit_)) {
    return;
  }
  resumeReadsImpl();
}

void HTTPSession::resumeReadsImpl() {
  VLOG(4) << *this << ": resuming reads";
  resetTimeout();
  reads_ = SocketState::UNPAUSED;
  codec_->setParserPaused(false);
  if (!isLoopCallbackScheduled()) {
    sock_->getEventBase()->runInLoop(this);
  }
}

bool
HTTPSession::hasMoreWrites() const {
  VLOG(10) << __PRETTY_FUNCTION__
    << " numActiveWrites_: " << numActiveWrites_
    << " pendingWrites_.empty(): " << pendingWrites_.empty()
    << " pendingWrites_.size(): " << pendingWrites_.size()
    << " txnEgressQueue_.empty(): " << txnEgressQueue_.empty();

  return (numActiveWrites_ != 0) ||
    !pendingWrites_.empty() || writeBuf_.front() ||
    !txnEgressQueue_.empty();
}

void HTTPSession::errorOnAllTransactions(
    ProxygenError err,
    const std::string& errorMsg) {
  std::vector<HTTPCodec::StreamID> ids;
  for (const auto& txn: transactions_) {
    ids.push_back(txn.first);
  }
  errorOnTransactionIds(ids, err, errorMsg);
}

void HTTPSession::errorOnTransactionIds(
  const std::vector<HTTPCodec::StreamID>& ids,
  ProxygenError err,
  const std::string& errorMsg) {
  std::string extraErrorMsg;
  if (!errorMsg.empty()) {
    extraErrorMsg = folly::to<std::string>(". ", errorMsg);
  }

  for (auto id: ids) {
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
      folly::to<std::string>(getErrorString(err),
        " on transaction id: ", id,
        extraErrorMsg));
    ex.setProxygenError(err);
    errorOnTransactionId(id, std::move(ex));
  }
}

void HTTPSession::errorOnTransactionId(
    HTTPCodec::StreamID id,
    HTTPException ex) {
  auto txn = findTransaction(id);
  if (txn != nullptr) {
    txn->onError(std::move(ex));
  }
}

void HTTPSession::resumeTransactions() {
  CHECK(!inResume_);
  inResume_ = true;
  DestructorGuard g(this);
  auto resumeFn = [] (HTTP2PriorityQueue&, HTTPCodec::StreamID,
                      HTTPTransaction *txn, double) {
    if (txn) {
      txn->resumeEgress();
    }
    return false;
  };
  auto stopFn = [this] {
    return (transactions_.empty() || egressLimitExceeded());
  };

  txnEgressQueue_.iterateBFS(resumeFn, stopFn, true /* all */);
  inResume_ = false;
  if (pendingPause_) {
    VLOG(3) << "Pausing txn egress for " << *this;
    pendingPause_ = false;
    invokeOnAllTransactions(&HTTPTransaction::pauseEgress);
  }
}

void HTTPSession::onConnectionSendWindowOpen() {
  flowControlTimeout_.cancelTimeout();
  // We can write more now. Schedule a write.
  scheduleWrite();
}

void HTTPSession::onConnectionSendWindowClosed() {
  if(!txnEgressQueue_.empty()) {
    VLOG(4) << *this << " session stalled by flow control";
    if (sessionStats_) {
      sessionStats_->recordSessionStalled();
    }
  }
  DCHECK(!flowControlTimeout_.isScheduled());
  if (infoCallback_) {
    infoCallback_->onFlowControlWindowClosed(*this);
  }
  timeout_.scheduleTimeout(&flowControlTimeout_);
}

HTTPCodec::StreamID HTTPSession::getGracefulGoawayAck() const {
  if (!codec_->isReusable() || codec_->isWaitingToDrain()) {
    // TODO: just track last stream ID inside HTTPSession since this logic
    // is shared between HTTP/2 and SPDY
    return codec_->getLastIncomingStreamID();
  }
  VLOG(4) << *this << " getGracefulGoawayAck is reusable and not draining";
  // return the maximum possible stream id
  return std::numeric_limits<int32_t>::max();
}

void HTTPSession::invalidStream(HTTPCodec::StreamID stream, ErrorCode code) {
  if (!codec_->supportsParallelRequests()) {
    LOG(ERROR) << "Invalid stream on non-parallel codec.";
    return;
  }

  HTTPException err(HTTPException::Direction::INGRESS_AND_EGRESS,
                    folly::to<std::string>("invalid stream=", stream));
  // TODO: Below line will change for HTTP/2 -- just call a const getter
  // function for the status code.
  err.setCodecStatusCode(code);
  onError(stream, err, true);
}

void HTTPSession::onPingReplyLatency(int64_t latency) noexcept {
  if (infoCallback_ && latency >= 0) {
    infoCallback_->onPingReplySent(latency);
  }
}

uint64_t HTTPSession::getAppBytesWritten() noexcept {
 return sock_->getAppBytesWritten();
}

uint64_t HTTPSession::getRawBytesWritten() noexcept {
 return sock_->getRawBytesWritten();
}

void HTTPSession::onDeleteAckEvent() {
  if (readsShutdown()) {
    shutdownTransport(true, transactions_.empty());
  }
}

void HTTPSession::onEgressBuffered() {
  if (infoCallback_) {
    infoCallback_->onEgressBuffered(*this);
  }
}

void HTTPSession::onEgressBufferCleared() {
  if (infoCallback_) {
    infoCallback_->onEgressBufferCleared(*this);
  }
}

void HTTPSession::onReplaySafe() noexcept {
  sock_->setReplaySafetyCallback(nullptr);
  for (auto callback : waitingForReplaySafety_) {
    callback->onReplaySafe();
  }
  waitingForReplaySafety_.clear();
}

} // proxygen
