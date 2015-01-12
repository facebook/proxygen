/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/HTTPSession.h>

#include <chrono>
#include <folly/wangle/acceptor/ConnectionManager.h>
#include <folly/wangle/acceptor/SocketOptions.h>
#include <openssl/err.h>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/codec/HTTPChecks.h>
#include <proxygen/lib/http/session/HTTPSessionController.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>
#include <thrift/lib/cpp/async/TAsyncSSLSocket.h>

using apache::thrift::async::TAsyncSSLSocket;
using apache::thrift::async::TAsyncSocket;
using apache::thrift::async::TAsyncTransport;
using apache::thrift::async::WriteFlags;
using apache::thrift::transport::TTransportException;
using folly::IOBuf;
using folly::SocketAddress;
using folly::TransportInfo;
using std::pair;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

namespace {
static const uint32_t kMinReadSize = 1460;
static const uint32_t kMaxReadSize = 4000;

// Lower = higher latency, better prioritization
// Higher = lower latency, less prioritization
static const uint32_t kMaxWritesPerLoop = 32;

} // anonymous namespace

namespace proxygen {

uint32_t HTTPSession::kDefaultReadBufLimit = 65536;
uint32_t HTTPSession::kPendingWriteMax = 65536;

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
  // TAsyncTransport write failures are fatal.  If session_ is nullptr at this
  // point it means the TAsyncTransport implementation is not failing
  // subsequent writes correctly after an error.
  session_->onWriteSuccess(length_);
  delete this;
}

void
HTTPSession::WriteSegment::writeError(size_t bytesWritten,
                                      const TTransportException& ex) noexcept {
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
  AsyncTimeoutSet* transactionTimeouts,
  TAsyncTransport::UniquePtr sock,
  const SocketAddress& localAddr,
  const SocketAddress& peerAddr,
  HTTPSessionController* controller,
  unique_ptr<HTTPCodec> codec,
  const TransportInfo& tinfo,
  InfoCallback* infoCallback):
    localAddr_(localAddr),
    peerAddr_(peerAddr),
    sock_(std::move(sock)),
    controller_(controller),
    codec_(std::move(codec)),
    infoCallback_(infoCallback),
    writeTimeout_(this),
    transactionTimeouts_(CHECK_NOTNULL(transactionTimeouts)),
    transportInfo_(tinfo),
    direction_(codec_->getTransportDirection()),
    reads_(SocketState::PAUSED),
    writes_(SocketState::UNPAUSED),
    draining_(false),
    ingressUpgraded_(false),
    started_(false),
    writesDraining_(false),
    resetAfterDrainingWrites_(false),
    resetSocketOnShutdown_(false),
    ingressError_(false),
    inLoopCallback_(false) {

  codec_.add<HTTPChecks>();

  if (!codec_->supportsParallelRequests()) {
    // until we support upstream pipelining
    maxConcurrentIncomingStreams_ = 1;
    maxConcurrentOutgoingStreamsConfig_ = isDownstream() ? 0 : 1;
  }

  HTTPSettings* settings = codec_->getEgressSettings();
  if (settings) {
    settings->setSetting(SettingsId::MAX_CONCURRENT_STREAMS,
                         maxConcurrentIncomingStreams_);
  }
  if (codec_->supportsSessionFlowControl()) {
    connFlowControl_ = new FlowControlFilter(*this,
                                             writeBuf_,
                                             codec_.call(),
                                             kDefaultReadBufLimit);
    codec_.addFilters(std::unique_ptr<FlowControlFilter>(connFlowControl_));
  }

  if (!codec_->supportsPushTransactions()) {
    maxConcurrentPushTransactions_ = 0;
  }

  // If we receive IPv4-mapped IPv6 addresses, convert them to IPv4.
  localAddr_.tryConvertToIPv4();
  peerAddr_.tryConvertToIPv4();

  if (infoCallback_) {
    infoCallback_->onCreate(*this);
  }
  codec_.setCallback(this);

  if (controller_) {
    controller_->attachSession(this);
  }
}

HTTPSession::~HTTPSession() {
  VLOG(4) << *this << " closing";

  CHECK(transactions_.empty());
  CHECK(txnEgressQueue_.empty());
  DCHECK(!sock_->getReadCallback());

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
  codec_->generateConnectionPreface(writeBuf_);
  codec_->generateSettings(writeBuf_);
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
  HTTPSettings* settings = codec_->getEgressSettings();
  if (settings) {
    settings->setSetting(SettingsId::INITIAL_WINDOW_SIZE,
                         initialReceiveWindow_);
  }
  if (connFlowControl_) {
    connFlowControl_->setReceiveWindowSize(writeBuf_, receiveSessionWindowSize);
    scheduleWrite();
  }
}

void HTTPSession::setMaxConcurrentOutgoingStreams(uint32_t num) {
  CHECK(!started_);
  if (codec_->supportsParallelRequests()) {
    maxConcurrentOutgoingStreamsConfig_ = num;
  }
}

void HTTPSession::setMaxConcurrentPushTransactions(uint32_t num) {
  CHECK(!started_);
  if (codec_->supportsPushTransactions()) {
    maxConcurrentPushTransactions_ = num;
  }
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
  shutdownTransport(true, true);
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

void HTTPSession::notifyPendingEgress() noexcept {
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

void
HTTPSession::dropConnection() {
  VLOG(4) << "dropping " << *this;
  if (!sock_ || (readsShutdown() && writesShutdown())) {
    VLOG(4) << *this << " already shutdown";
    return;
  }

  setCloseReason(ConnectionCloseReason::SHUTDOWN);
  if (transactions_.empty() && !hasMoreWrites()) {
    shutdownTransport(true, true);
  } else {
    shutdownTransportWithReset(kErrorDropped);
  }
}

void
HTTPSession::dumpConnectionState(uint8_t loglevel) {
}

bool HTTPSession::isUpstream() const {
  return direction_ == TransportDirection::UPSTREAM;
}

bool HTTPSession::isDownstream() const {
  return direction_ == TransportDirection::DOWNSTREAM;
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

void
HTTPSession::processReadData() {
  // Pass the ingress data through the codec to parse it. The codec
  // will invoke various methods of the HTTPSession as callbacks.
  const IOBuf* currentReadBuf;
  // It's possible for the last buffer in a chain to be empty here.
  // TAsyncTransport saw fd activity so asked for a read buffer, but it was
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
      && transportInfo_.ssl && transactionSeqNo_ == 0 && readBuf_.empty()) {
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
HTTPSession::readError(
    const TTransportException& ex) noexcept {
  DestructorGuard guard(this);
  VLOG(4) << "read error on " << *this << ": " << ex.what();
  if (infoCallback_ && (
        ERR_GET_LIB(ex.getErrno()) == ERR_LIB_USER &&
        ERR_GET_REASON(ex.getErrno()) ==
        (int)TAsyncSSLSocket::SSL_CLIENT_RENEGOTIATION_ATTEMPT)) {
    infoCallback_->onIngressError(*this, kErrorClientRenegotiation);
  }

  // We're definitely finished reading. Don't close the write side
  // of the socket if there are outstanding transactions, though.
  // Instead, give the transactions a chance to produce any remaining
  // output.
  if (ERR_GET_LIB(ex.getErrno()) == ERR_LIB_SSL) {
    transportInfo_.sslError = ERR_GET_REASON(ex.getErrno());
  }
  setCloseReason(ConnectionCloseReason::IO_READ_ERROR);
  shutdownTransport(true, transactions_.empty());
}

HTTPTransaction*
HTTPSession::newPushedTransaction(HTTPCodec::StreamID assocStreamId,
                                  HTTPTransaction::PushHandler* handler,
                                  int8_t priority) noexcept {
  if (!codec_->supportsPushTransactions()) {
    return nullptr;
  }
  CHECK(isDownstream());
  CHECK_NOTNULL(handler);
  if (draining_ || (pushedTxns_ >= maxConcurrentPushTransactions_)) {
    // This session doesn't support any more push transactions
    // This could be an actual problem - since a single downstream SPDY session
    // might be connected to N upstream hosts, each of which send M pushes,
    // which exceeds the limit.
    // should we queue?
    return nullptr;
  }

  HTTPTransaction* txn = createTransaction(codec_->createStream(),
                                           assocStreamId,
                                           priority);
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
    return settings->getSetting(SettingsId::INITIAL_WINDOW_SIZE, 65536);
  }
  return 65536;
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
            << ", kPendingWriteMax=" << kPendingWriteMax;
    txn->pauseEgress();
  }
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

  txn = createTransaction(streamID,
                          assocStreamID,
                          msg ? msg->getPriority() : 0);

  if (!txn) {
    // This could happen if the socket is bad.
    return nullptr;
  }

  if (assocStream && !assocStream->onPushedTransaction(txn)) {
    VLOG(1) << "Failed to add pushed transaction " << streamID << " on "
            << *this;
    HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
      "Failed to add pushed transaction ", streamID);
    ex.setCodecStatusCode(ErrorCode::REFUSED_STREAM);
    onError(streamID, ex, true);
    return nullptr;
  }

  if (!codec_->supportsParallelRequests() && transactions_.size() > 1) {
    // The previous transaction hasn't completed yet. Pause reads until
    // it completes; this requires pausing both transactions.
    DCHECK(transactions_.size() == 2);
    auto prevTxn = &transactions_.begin()->second;
    if (!prevTxn->isIngressPaused()) {
      DCHECK(prevTxn->isIngressComplete());
      prevTxn->pauseIngress();
    }
    DCHECK(liveTransactions_ == 1);
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
    transportInfo_.sslCipher ? transportInfo_.sslCipher : nullptr;
  msg->setSecureInfo(transportInfo_.sslVersion, sslCipher);
  msg->setSecure(transportInfo_.ssl);

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
                    unique_ptr<IOBuf> chain) {
  DestructorGuard dg(this);
  // The codec's parser detected part of the ingress message's
  // entity-body.
  HTTPTransaction* txn = findTransaction(streamID);
  if (!txn) {
    invalidStream(streamID);
    return;
  }
  auto oldSize = pendingReadSize_;
  pendingReadSize_ += chain->computeChainDataLength();
  txn->onIngressBody(std::move(chain));
  if (oldSize < pendingReadSize_) {
    // Transaction must have buffered something and not called
    // notifyBodyProcessed() on it.
    VLOG(4) << *this << " Enqueued ingress. Ingress buffer uses "
            << pendingReadSize_  << " of "  << kDefaultReadBufLimit
            << " bytes.";
    if (pendingReadSize_ > kDefaultReadBufLimit &&
        oldSize <= kDefaultReadBufLimit) {
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
    "Stream aborted, streamID=", streamID, ", code=", getErrorCodeString(code));
  ex.setProxygenError(kErrorStreamAbort);
  DestructorGuard dg(this);
  if (isDownstream() && txn->getAssocTxnId() == 0 &&
      code == ErrorCode::CANCEL) {
    // Cancelling the assoc txn cancels all push txns
    for (auto pushTxnId : txn->getPushedTransactions()) {
      auto pushTxn = findTransaction(pushTxnId);
      DCHECK(pushTxn != nullptr);
      pushTxn->onError(ex);
    }
  }
  txn->onError(ex);
}

void HTTPSession::onGoaway(uint64_t lastGoodStreamID,
                           ErrorCode code) {
  DestructorGuard g(this);
  VLOG(4) << "GOAWAY on " << *this << ", code=" << getErrorCodeString(code);

  setCloseReason(ConnectionCloseReason::GOAWAY);

  // Drain active transactions and prevent new transactions
  drain();

  // Abort transactions which have been initiated but not created
  // successfully at the remote end. Upstream transactions are created
  // with odd transaction IDs and downstream transactions with even IDs.
  vector<HTTPCodec::StreamID> ids;
  for (const auto& txn: transactions_) {
    auto streamID = txn.first;
    if (((bool)(streamID & 0x01) == isUpstream()) &&
        (streamID > lastGoodStreamID)) {
      ids.push_back(streamID);
    }
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
  for (auto& setting: settings) {
    if (setting.id == SettingsId::INITIAL_WINDOW_SIZE) {
      onSetSendWindow(setting.value);
    } else if (setting.id == SettingsId::MAX_CONCURRENT_STREAMS) {
      onSetMaxInitiatedStreams(setting.value);
    }
  }
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

void HTTPSession::pauseIngress(HTTPTransaction* txn) noexcept {
  VLOG(4) << *this << " pausing streamID=" << txn->getID() <<
    ", liveTransactions_ was " << liveTransactions_;
  CHECK(liveTransactions_ > 0);
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
    VLOG(4) << *this << " creating direct error handler";
    auto handler = getTransactionTimeoutHandler(txn);
    txn->setHandler(handler);
    if (infoCallback_) {
      infoCallback_->onIngressError(*this, kErrorTimeout);
    }
  }

  // Tell the transaction about the timeout.  The transaction will
  // communicate the timeout to the handler, and the handler will
  // decide how to proceed.
  txn->onIngressTimeout();
}

void HTTPSession::sendHeaders(HTTPTransaction* txn,
                              const HTTPMessage& headers,
                              HTTPHeaderSize* size) noexcept {
  CHECK(started_);
  if (shouldShutdown()) {
    // For HTTP/1.1, add Connection: close
    drainImpl();
  }
  const bool wasReusable = codec_->isReusable();
  const uint64_t oldOffset = sessionByteOffset();
  codec_->generateHeader(writeBuf_,
                         txn->getID(),
                         headers,
                         txn->getAssocTxnId(),
                         false, // eom
                         size);
  const uint64_t newOffset = sessionByteOffset();

  // only do it for downstream now to bypass handling upstream reuse cases
  if (isDownstream() &&
      newOffset > oldOffset &&
      // catch 100-ish response?
      !txn->testAndSetFirstHeaderByteSent() && byteEventTracker_) {
    byteEventTracker_->addFirstHeaderByteEvent(newOffset, txn);
  }

  if (size) {
    VLOG(4) << *this << " sending headers, size=" << size->compressed
            << ", uncompressedSize=" << size->uncompressed;
  }
  scheduleWrite();
  onHeadersSent(headers, wasReusable);
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
                                            includeEOM);
  // In the rare case we are within bodyLen over the limit, and this
  // write will block the socket, then this txn will see resume and pause.
  updateWriteBufSize(-bodyLen);
  if (encodedSize > 0 && !txn->testAndSetFirstByteSent() && byteEventTracker_) {
    byteEventTracker_->addFirstBodyByteEvent(offset, txn);
  }
  if (includeEOM) {
    if (!txn->testAndSetFirstByteSent()) {
      txn->onEgressBodyFirstByte();
    }
    if (encodedSize > 0 && byteEventTracker_) {
      byteEventTracker_->addLastByteEvent(txn, sessionByteOffset(),
                                          sock_->isEorTrackingEnabled());
    }

    VLOG(4) << *this << " sending EOM in body for streamID=" << txn->getID();
    onEgressMessageFinished(txn);
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

      // Just for safety, bump the refcount on this session to keep it
      // live until the loopCb runs
      auto dg = new DestructorGuard(this);
      sock_->getEventBase()->runInLoop([this, dg] {
          VLOG(4) << *this << " shutdown from onEgressMessageFinished";
          bool shutdownReads = isDownstream() && !ingressUpgraded_;
          shutdownTransport(shutdownReads, true);
          delete dg;
        }, true);
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
  if (!txn->testAndSetFirstByteSent()) {
    txn->onEgressBodyFirstByte();
  }
  txn->onEgressBodyLastByte();
  if (encodedSize > 0 && byteEventTracker_) {
    byteEventTracker_->addLastByteEvent(txn, sessionByteOffset(),
                                        sock_->isEorTrackingEnabled());
  }
  // in case encodedSize == 0 we won't get TTLBA which is acceptable
  // noting the fact that we don't have a response body

  onEgressMessageFinished(txn);
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
  if (!txn->isIngressPaused()) {
    VLOG(4) << *this << " removing streamID=" << streamID <<
        ", liveTransactions was " << liveTransactions_;
    CHECK(liveTransactions_ > 0);
    liveTransactions_--;
  } else {
    VLOG(4) << *this << " removing streamID=" << streamID;
  }
  if (txn->isPushed()) {
    CHECK(pushedTxns_ > 0);
    pushedTxns_--;
    auto assocTxn = findTransaction(txn->getAssocTxnId());
    if (assocTxn) {
      assocTxn->removePushedTransaction(streamID);
    }
  }
  decrementTransactionCount(txn, true, true);
  transactions_.erase(it);
  if (infoCallback_) {
    if (transactions_.empty()) {
      infoCallback_->onDeactivateConnection(*this);
    } else {
      infoCallback_->onTransactionDetached(*this);
    }
  }
  if (!readsShutdown()) {
    if (!codec_->supportsParallelRequests() && !transactions_.empty()) {
      // If we had more than one transaction, then someone tried to pipeline and
      // we paused reads
      DCHECK(transactions_.size() == 1);
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
  CHECK(pendingReadSize_ >= bytes);
  auto oldSize = pendingReadSize_;
  pendingReadSize_ -= bytes;
  VLOG(4) << *this << " Dequeued " << bytes << " bytes of ingress. "
    << "Ingress buffer uses " << pendingReadSize_  << " of "
    << kDefaultReadBufLimit << " bytes.";
  if (connFlowControl_ &&
      connFlowControl_->ingressBytesProcessed(writeBuf_, bytes)) {
    scheduleWrite();
  }
  if (oldSize > kDefaultReadBufLimit &&
      pendingReadSize_ <= kDefaultReadBufLimit) {
    resumeReads();
  }
}

void
HTTPSession::notifyEgressBodyBuffered(uint32_t bytes) noexcept {
  updateWriteBufSize(bytes);
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

bool HTTPSession::getCurrentTransportInfo(TransportInfo* tinfo) {
  TAsyncSocket* sock = dynamic_cast<TAsyncSocket*>(sock_.get());
  if (sock) {
    tinfo->initWithSocket(sock);
    // some fields are the same with the setup transport info
    tinfo->setupTime = transportInfo_.setupTime;
    tinfo->ssl = transportInfo_.ssl;
    tinfo->sslSetupTime = transportInfo_.sslSetupTime;
    tinfo->sslVersion = transportInfo_.sslVersion;
    tinfo->sslCipher = transportInfo_.sslCipher;
    tinfo->sslResume = transportInfo_.sslResume;
    tinfo->sslNextProtocol = transportInfo_.sslNextProtocol;
    tinfo->sslError = transportInfo_.sslError;
#if defined(__linux__) || defined(__FreeBSD__)
    // update connection transport info with the latest RTT
    if (tinfo->tcpinfo.tcpi_rtt > 0) {
      transportInfo_.tcpinfo.tcpi_rtt = tinfo->tcpinfo.tcpi_rtt;
      transportInfo_.rtt = std::chrono::microseconds(tinfo->tcpinfo.tcpi_rtt);
    }
#endif
    return true;
  }
  return false;
}

void HTTPSession::setByteEventTracker(
    std::unique_ptr<ByteEventTracker> byteEventTracker) {
  byteEventTracker_ = std::move(byteEventTracker);
  byteEventTracker_->setCallback(this);
  byteEventTracker_->setTTLBAStats(sessionStats_);
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
    uint32_t allowed = std::numeric_limits<uint32_t>::max();
    if (connFlowControl_) {
      allowed = connFlowControl_->getAvailableSend();
      if (allowed == 0) {
        VLOG(4) << "Session-level send window is full, skipping "
                << "body writes this loop";
        break;
      }
    }
    auto txn = txnEgressQueue_.top();
    // returns true if there is more egress pending for this txn
    if (txn->onWriteReady(allowed) || writeBuf_.front()) {
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
        *cork = !txnEgressQueue_.empty();
        if (sessionStats_) {
          sessionStats_->recordTTLBAIOBSplitByEom();
        }
        return writeBuf_.split(needed);
      } else {
        CHECK(needed == writeBuf_.chainLength());
      }
    }
  }

  // cork if there are txns with pending egress
  *cork = !txnEgressQueue_.empty();
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
  folly::ScopeGuard scopeg = folly::makeGuard(
    [this] { inLoopCallback_ = false;});
  VLOG(4) << *this << " in loop callback";

  for (uint32_t i = 0; writesUnpaused() && i < kMaxWritesPerLoop; ++i) {
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
      transactionTimeouts_->scheduleTimeout(&writeTimeout_);
    }
    numActiveWrites_++;
    VLOG(4) << *this << " writing " << len << ", activeWrites="
             << numActiveWrites_ << " cork=" << cork << " eom=" << eom;
    bytesScheduled_ += len;
    sock_->writeChain(segment, std::move(writeBuf), segment->getFlags());
    if (numActiveWrites_ > 0) {
      updateWriteBufSize(len);
      updateWriteCount();
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
      sock_->setReadCallback(this);
    }
  }
  checkForShutdown();
}

void
HTTPSession::scheduleWrite() {
  // Do all the network writes for this connection in one batch at
  // the end of the current event loop iteration.  Writing in a
  // batch helps us packetize the network traffic more efficiently,
  // as well as saving a few system calls.
  if (writesUnpaused() && !isLoopCallbackScheduled() &&
      (writeBuf_.front() || !txnEgressQueue_.empty())) {
    VLOG(4) << *this << " scheduling write callback";
    sock_->getEventBase()->runInLoop(this);
  }
}

bool HTTPSession::egressLimitExceeded() const {
  return pendingWriteSize_ >= kPendingWriteMax;
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
  DCHECK(delta >= 0 || uint64_t(-delta) <= pendingWriteSize_);
  bool wasExceeded = egressLimitExceeded();
  pendingWriteSize_ += delta;

  if (egressLimitExceeded() && !wasExceeded) {
    // Exceeded limit. Pause reading on the incoming stream.
    VLOG(3) << "Pausing txn egress for " << *this;
    invokeOnAllTransactions(&HTTPTransaction::pauseEgress);
  } else if (!egressLimitExceeded() && wasExceeded) {
    // Dropped below limit. Resume reading on the incoming stream if needed.
    VLOG(3) << "Resuming txn egress for " << *this;
    invokeOnAllTransactions(&HTTPTransaction::resumeEgress);
  }
}

void
HTTPSession::shutdownTransport(bool shutdownReads,
                               bool shutdownWrites) {
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
  if (transportInfo_.sslError) {
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
  } else {
    error = kErrorEOF;
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
    sock_->setReadCallback(nullptr);
    reads_ = SocketState::SHUTDOWN;
    if (!transactions_.empty() && error == kErrorConnectionReset) {
      if (infoCallback_ != nullptr) {
        infoCallback_->onIngressError(*this, error);
      }
    } else if (error == kErrorEOF) {
      // Report to the codec that the ingress stream has ended
      codec_->onIngressEOF();
    }
    // Once reads are shutdown the parser should stop processing
    codec_->setParserPaused(true);
  }

  if (notifyIngressShutdown || notifyEgressShutdown) {
    auto dir = (notifyIngressShutdown && notifyEgressShutdown) ?
      HTTPException::Direction::INGRESS_AND_EGRESS :
      (notifyIngressShutdown ? HTTPException::Direction::INGRESS :
         HTTPException::Direction::EGRESS);
    HTTPException ex(dir, "Shutdown transport: ", getErrorString(error));
    ex.setProxygenError(error);
    invokeOnAllTransactions(&HTTPTransaction::onError, ex);
  }

  // Close the socket only after the onError() callback on the txns
  // and handler has been detached.
  checkForShutdown();
}

void HTTPSession::shutdownTransportWithReset(ProxygenError errorCode) {
  DestructorGuard guard(this);
  VLOG(4) << "shutdownTransportWithReset";

  if (isLoopCallbackScheduled()) {
    cancelLoopCallback();
  }
  if (!readsShutdown()) {
    sock_->setReadCallback(nullptr);
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

  errorOnAllTransactions(errorCode);
  // drainByteEvents() can call detach(txn), which can in turn call
  // shutdownTransport if we were already draining. To prevent double
  // calling onError() to the transactions, we call drainByteEvents()
  // after we've given the explicit error.
  if (byteEventTracker_) {
    byteEventTracker_->drainByteEvents();
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
    sock_->setReadCallback(nullptr);
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
                               int8_t priority) {
  if (!sock_->good() || transactions_.count(streamID)) {
    // Refuse to add a transaction on a closing session or if a
    // transaction of that ID already exists.
    return nullptr;
  }

  if (transactions_.empty() && infoCallback_) {
    infoCallback_->onActivateConnection(*this);
  }

  auto matchPair = transactions_.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(streamID),
    std::forward_as_tuple(
      direction_, streamID, transactionSeqNo_, *this,
      txnEgressQueue_, transactionTimeouts_, sessionStats_,
      codec_->supportsStreamFlowControl(),
      initialReceiveWindow_,
      getCodecSendWindowSize(),
      priority, assocStreamID));

  CHECK(matchPair.second) << "Emplacement failed, despite earlier "
    "existence check.";

  HTTPTransaction* txn = &matchPair.first->second;

  VLOG(4) << *this << " adding streamID=" << txn->getID()
          << ", liveTransactions was " << liveTransactions_;

  ++liveTransactions_;
  ++transactionSeqNo_;
  txn->setReceiveWindow(receiveStreamWindowSize_);

  if ((isUpstream() && !txn->isPushed()) ||
      (isDownstream() && txn->isPushed())) {
    outgoingStreams_++;
  } else {
    incomingStreams_++;
  }

  if (txn->isPushed()) {
    pushedTxns_++;
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
    transactionTimeouts_->scheduleTimeout(&writeTimeout_);
  }

  if (infoCallback_) {
    infoCallback_->onWrite(*this, bytesWritten);
  }

  VLOG(5) << "total bytesWritten_: " << bytesWritten_;

  if (byteEventTracker_) {
    byteEventTracker_->processByteEvents(bytesWritten_,
                                         sock_->isEorTrackingEnabled());
  }

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
    updateWriteBufSize(-bytesWritten);
    updateWriteCount();
    // PRIO_FIXME: this is done because of the corking business...
    //             in the future we may want to have a pull model
    //             whereby the socket asks us for a given amount of
    //             data to send...
    if (numActiveWrites_ == 0 && hasMoreWrites()) {
      runLoopCallback();
    }
  }
  onWriteCompleted();
}

void
HTTPSession::onWriteError(size_t bytesWritten,
                          const TTransportException& ex) {
  VLOG(4) << *this << " write error: " << ex.what();
  if (infoCallback_) {
    infoCallback_->onWrite(*this, bytesWritten);
  }

  // Save the SSL error, if there was one.  It will be recorded later
  if (ERR_GET_LIB(ex.getErrno()) == ERR_LIB_SSL) {
    transportInfo_.sslError = ERR_GET_REASON(ex.getErrno());
  }

  setCloseReason(ConnectionCloseReason::IO_WRITE_ERROR);
  shutdownTransportWithReset(kErrorWrite);
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
    codec_->generateGoaway(writeBuf_,
                           codec_->getLastIncomingStreamID(),
                           error.getCodecStatusCode());
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
       pendingReadSize_ <= kDefaultReadBufLimit)) {
    return;
  }
  VLOG(4) << *this << ": pausing reads";
  if (infoCallback_) {
    infoCallback_->onIngressPaused(*this);
  }
  cancelTimeout();
  sock_->setReadCallback(nullptr);
  reads_ = SocketState::PAUSED;
}

void
HTTPSession::resumeReads() {
  if (!readsPaused() ||
      (codec_->supportsParallelRequests() &&
       pendingReadSize_ > kDefaultReadBufLimit)) {
    return;
  }
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

void HTTPSession::errorOnAllTransactions(ProxygenError err) {
  std::vector<HTTPCodec::StreamID> ids;
  for (const auto& txn: transactions_) {
    ids.push_back(txn.first);
  }
  errorOnTransactionIds(ids, err);
}

void HTTPSession::errorOnTransactionIds(
  const std::vector<HTTPCodec::StreamID>& ids,
  ProxygenError err) {

  for (auto id: ids) {
    auto txn = findTransaction(id);
    if (txn != nullptr) {
      HTTPException ex(HTTPException::Direction::INGRESS_AND_EGRESS,
        getErrorString(err), " on transaction id: ", id);
      ex.setProxygenError(err);
      txn->onError(ex);
    }
  }
}

void HTTPSession::onConnectionSendWindowOpen() {
  // We can write more now. Schedule a write.
  scheduleWrite();
}

HTTPCodec::StreamID HTTPSession::getGracefulGoawayAck() const {
  if (!codec_->isReusable() || codec_->isWaitingToDrain()) {
    // TODO: just track last stream ID inside HTTPSession since this logic
    // is shared between HTTP/2 and SPDY
    return codec_->getLastIncomingStreamID();
  }
  // return the maximum possible stream id
  return std::numeric_limits<int32_t>::max();
}

void HTTPSession::invalidStream(HTTPCodec::StreamID stream, ErrorCode code) {
  if (!codec_->supportsParallelRequests()) {
    LOG(ERROR) << "Invalid stream on non-parallel codec.";
    return;
  }

  HTTPException err(HTTPException::Direction::INGRESS_AND_EGRESS,
                    "invalid stream=", stream);
  // TODO: Below line will change for HTTP/2 -- just call a const getter
  // function for the status code.
  err.setCodecStatusCode(code);
  onError(stream, err, true);
}

void HTTPSession::onPingReplyLatency(int64_t latency) noexcept {
  if (infoCallback_ && latency >= 0) {
    infoCallback_->onPingReply(latency);
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

} // proxygen
