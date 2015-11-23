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

#include <folly/IntrusiveList.h>
#include <wangle/acceptor/ManagedConnection.h>
#include <wangle/acceptor/TransportInfo.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/HHWheelTimer.h>
#include <proxygen/lib/http/HTTPConstants.h>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/codec/FlowControlFilter.h>
#include <proxygen/lib/http/codec/HTTPCodec.h>
#include <proxygen/lib/http/codec/HTTPCodecFilter.h>
#include <proxygen/lib/http/session/ByteEventTracker.h>
#include <proxygen/lib/http/session/HTTPEvent.h>
#include <proxygen/lib/http/session/HTTPTransaction.h>
#include <proxygen/lib/utils/Time.h>
#include <queue>
#include <set>
#include <folly/io/async/AsyncSocket.h>
#include <vector>

namespace proxygen {

class HTTPSessionController;
class HTTPSessionStats;

class HTTPSession:
  private FlowControlFilter::Callback,
  private HTTPCodec::Callback,
  private folly::EventBase::LoopCallback,
  public ByteEventTracker::Callback,
  public HTTPTransaction::Transport,
  public folly::AsyncTransportWrapper::ReadCallback,
  public wangle::ManagedConnection,
  public folly::AsyncTransportWrapper::BufferCallback {
 public:
  typedef std::unique_ptr<HTTPSession, Destructor> UniquePtr;

  /**
   * Optional callback interface that the HTTPSession
   * notifies of connection lifecycle events.
   */
  class InfoCallback {
   public:
    virtual ~InfoCallback() {}

    // Note: you must not start any asynchronous work from onCreate()
    virtual void onCreate(const HTTPSession&) = 0;
    virtual void onIngressError(const HTTPSession&, ProxygenError) = 0;
    virtual void onRead(const HTTPSession&, size_t bytesRead) = 0;
    virtual void onWrite(const HTTPSession&, size_t bytesWritten) = 0;
    virtual void onRequestBegin(const HTTPSession&) = 0;
    virtual void onRequestEnd(const HTTPSession&,
                              uint32_t maxIngressQueueSize) = 0;
    virtual void onActivateConnection(const HTTPSession&) = 0;
    virtual void onDeactivateConnection(const HTTPSession&,
                                        const TransactionInfo&) = 0;
    // Note: you must not start any asynchronous work from onDestroy()
    virtual void onDestroy(const HTTPSession&) = 0;
    virtual void onIngressMessage(const HTTPSession&,
                                  const HTTPMessage&) = 0;
    virtual void onIngressLimitExceeded(const HTTPSession&) = 0;
    virtual void onIngressPaused(const HTTPSession&) = 0;
    virtual void onTransactionDetached(const HTTPSession&,
                                       const TransactionInfo&) = 0;
    virtual void onPingReplySent(int64_t latency) = 0;
    virtual void onPingReplyReceived() = 0;
    virtual void onSettingsOutgoingStreamsFull(const HTTPSession&) = 0;
    virtual void onSettingsOutgoingStreamsNotFull(const HTTPSession&) = 0;
    virtual void onFlowControlWindowClosed(const HTTPSession&) = 0;
    virtual void onEgressBuffered(const HTTPSession&) = 0;
  };

  class WriteTimeout :
      public folly::HHWheelTimer::Callback {
   public:
    explicit WriteTimeout(HTTPSession* session) : session_(session) {}
    ~WriteTimeout() override {}

    void timeoutExpired() noexcept override { session_->writeTimeoutExpired(); }
   private:
    HTTPSession* session_;
  };

  class FlowControlTimeout : public folly::HHWheelTimer::Callback {
   public:
    explicit FlowControlTimeout(HTTPSession* session) : session_(session) {}
    ~FlowControlTimeout() override {}

    void timeoutExpired() noexcept override {
      session_->flowControlTimeoutExpired();
    }
   private:
    HTTPSession* session_;
  };

  /**
   * Set the read buffer limit to be used for all new HTTPSession objects.
   */
  static void setDefaultReadBufferLimit(uint32_t limit) {
    kDefaultReadBufLimit = limit;
    VLOG(1) << "read buffer limit: " << int(limit / 1000) << "KB";
  }

  /**
   * Set the maximum egress body size for any outbound body bytes per loop,
   * when there are > 1 transactions.
   */
  static void setFlowControlledBodySizeLimit(uint64_t limit) {
    egressBodySizeLimit_ = limit;
  }

  void setInfoCallback(InfoCallback* callback);

  void setSessionStats(HTTPSessionStats* stats);

  folly::AsyncTransportWrapper* getTransport() {
    return sock_.get();
  }

  const folly::AsyncTransportWrapper* getTransport() const {
    return sock_.get();
  }

  bool hasActiveTransactions() const {
    return !transactions_.empty();
  }

  /**
   * Returns true iff a new outgoing transaction can be made on this session
   */
  bool supportsMoreTransactions() const {
    return (outgoingStreams_ < maxConcurrentOutgoingStreamsConfig_) &&
      (outgoingStreams_ < maxConcurrentOutgoingStreamsRemote_);
  }

  uint32_t getNumOutgoingStreams() const {
    return outgoingStreams_;
  }

  uint32_t getHistoricalMaxOutgoingStreams() const {
    return historicalMaxOutgoingStreams_;
  }

  uint32_t getNumIncomingStreams() const {
    return incomingStreams_;
  }

  uint32_t getMaxConcurrentOutgoingStreams() const {
    return std::min(maxConcurrentOutgoingStreamsConfig_,
                    maxConcurrentOutgoingStreamsRemote_);
  }

  uint32_t getMaxConcurrentPushTransactions() const {
    return maxConcurrentPushTransactions_;
  }

  bool readsUnpaused() const {
    return reads_ == SocketState::UNPAUSED;
  }

  bool readsPaused() const {
    return reads_ == SocketState::PAUSED;
  }

  bool readsShutdown() const {
    return reads_ == SocketState::SHUTDOWN;
  }

  bool writesUnpaused() const {
    return writes_ == SocketState::UNPAUSED;
  }

  bool writesPaused() const {
    return writes_ == SocketState::PAUSED;
  }

  bool writesShutdown() const {
    return writes_ == SocketState::SHUTDOWN;
  }

  bool writesDraining() const {
    return writesDraining_;
  }

  const HTTPSessionController* getController() const { return controller_; }
  HTTPSessionController* getController() { return controller_; }

  /**
   * ManagedConnection::getIdleTime()
   */
  std::chrono::milliseconds getIdleTime() const override {
    if (timePointInitialized(latestActive_)) {
      return secondsSince(latestActive_);
    } else {
      return std::chrono::milliseconds(0);
    }
  }

  /**
   * Start closing the socket.
   * @param shutdownReads  Whether to close the read side of the
   * socket. All transactions which are not ingress complete will receive
   * an error.
   * @param shutdownWrites Whether to close the write side of the
   * socket. All transactions which are not egress complete will receive
   * an error.
   */
  void shutdownTransport(bool shutdownReads = true,
                         bool shutdownWrites = true);

  /**
   * Immediately close the socket in both directions, discarding any
   * queued writes that haven't yet been transferred to the kernel,
   * and send a RST to the client.
   * All transactions receive onWriteError.
   *
   * @param errorCode  Error code sent with the onWriteError to transactions.
   * @param errorMsg   Error string included in the final error msg.
   */
  void shutdownTransportWithReset(ProxygenError errorCode,
                                  const std::string& errorMsg = "");

  ConnectionCloseReason getConnectionCloseReason() const {
    return closeReason_;
  }

  HTTPCodecFilterChain& getCodecFilterChain() {
    return codec_;
  }

  /**
   * Set flow control properties on the session.
   *
   * @param initialReceiveWindow      size of initial receive window
   *                                  for all ingress streams; set via
   *                                  the initial SETTINGS frame
   * @param receiveStreamWindowSize   per-stream receive window for NEW streams;
   *                                  sent via a WINDOW_UPDATE frame
   * @param receiveSessionWindowSize  per-session receive window; sent
   *                                  via a WINDOW_UPDATE frame
   */
  void setFlowControl(
   size_t initialReceiveWindow,
   size_t receiveStreamWindowSize,
   size_t receiveSessionWindowSize);

  /**
   * Set the maximum number of outgoing transactions this session can open
   * at once. Note: you can only call function before startNow() is called
   * since the remote side can change this value.
   */
  void setMaxConcurrentOutgoingStreams(uint32_t num);

  /*
   * The maximum number of concurrent push transactions that can be supported
   * on this session.
   */
  void setMaxConcurrentPushTransactions(uint32_t num);

  /**
   * Set the maximum number of transactions the remote can open at once.
   */
  void setMaxConcurrentIncomingStreams(uint32_t num);

  /**
   * Get/Set the number of egress bytes this session will buffer before
   * pausing all transactions' egress.
   */
  static uint32_t getPendingWriteMax() {
    return kPendingWriteMax;
  }

  static void setPendingWriteMax(uint32_t max) {
    kPendingWriteMax = max;
  }

  /**
   * Start reading from the transport and send any introductory messages
   * to the remote side. This function must be called once per session to
   * begin reads.
   */
  void startNow();

  /**
   * Returns true if this session is draining. This can happen if drain()
   * is called explicitly, if a GOAWAY frame is received, or during shutdown.
   */
  bool isDraining() const override { return draining_; }

  /**
   * Causes a ping to be sent on the session. If the underlying protocol
   * doesn't support pings, this will return 0. Otherwise, it will return
   * the number of bytes written on the transport to send the ping.
   */
  size_t sendPing();

  // ManagedConnection methods
  void timeoutExpired() noexcept override {
      readTimeoutExpired();
  }
  void describe(std::ostream& os) const override;
  bool isBusy() const override;
  void notifyPendingShutdown() override;
  void closeWhenIdle() override;
  void dropConnection() override;
  void dumpConnectionState(uint8_t loglevel) override;

  bool isUpstream() const;
  bool isDownstream() const;

  uint64_t getNumTxnServed() const {
    return numTxnServed_;
  }

  std::chrono::seconds getLatestIdleTime() const {
    DCHECK_GT(numTxnServed_, 0) << "No idle time for the first transcation";
    DCHECK(latestActive_ > TimePoint::min());
    return latestIdleDuration_;
  }

  // from AsyncTransport::BufferCallback
  virtual void onEgressBuffered() override;

 protected:
  /**
   * HTTPSession is an abstract base class and cannot be instantiated
   * directly. If you want to handle requests and send responses (act as a
   * server), construct a HTTPDownstreamSession. If you want to make
   * requests and handle responses (act as a client), construct a
   * HTTPUpstreamSession.
   *
   * @param transactionTimeouts  Timeout for each transaction in the session.
   * @param sock                 An open socket on which any applicable TLS
   *                               handshaking has been completed already.
   * @param localAddr            Address and port of the local end of
   *                               the socket.
   * @param peerAddr             Address and port of the remote end of
   *                               the socket.
   * @param controller           Controller which can create the handler for
   *                               a new transaction.
   * @param codec                A codec with which to parse/generate messages
   *                               in whatever HTTP-like wire format this
   *                               session needs.
   * @param tinfo                Struct containing the transport's TCP/SSL
   *                               level info.
   * @param InfoCallback         Optional callback to be informed of session
   *                               lifecycle events.
   */
  HTTPSession(
      folly::HHWheelTimer::SharedPtr transactionTimeouts,
      folly::AsyncTransportWrapper::UniquePtr sock,
      const folly::SocketAddress& localAddr,
      const folly::SocketAddress& peerAddr,
      HTTPSessionController* controller,
      std::unique_ptr<HTTPCodec> codec,
      const wangle::TransportInfo& tinfo,
      InfoCallback* infoCallback = nullptr);

  HTTPSession(
      folly::HHWheelTimer* transactionTimeouts,
      folly::AsyncTransportWrapper::UniquePtr sock,
      const folly::SocketAddress& localAddr,
      const folly::SocketAddress& peerAddr,
      HTTPSessionController* controller,
      std::unique_ptr<HTTPCodec> codec,
      const wangle::TransportInfo& tinfo,
      InfoCallback* infoCallback = nullptr)
    : HTTPSession(folly::HHWheelTimer::SharedPtr(transactionTimeouts),
                  std::move(sock),
                  localAddr,
                  peerAddr,
                  controller,
                  std::move(codec),
                  tinfo,
                  infoCallback) {}

  ~HTTPSession() override;

  /**
   * Called by onHeadersComplete(). This function allows downstream and
   * upstream to do any setup (like preparing a handler) when headers are
   * first received from the remote side on a given transaction.
   */
  virtual void setupOnHeadersComplete(HTTPTransaction* txn,
                                      HTTPMessage* msg) = 0;

  /**
   * Called by handleErrorDirectly (when handling parse errors) if the
   * transaction has no handler.
   */
  virtual HTTPTransaction::Handler* getParseErrorHandler(
    HTTPTransaction* txn, const HTTPException& error) = 0;

  /**
   * Called by transactionTimeout if the transaction has no handler.
   */
  virtual HTTPTransaction::Handler* getTransactionTimeoutHandler(
    HTTPTransaction* txn) = 0;

  /**
   * Invoked when headers have been sent.
   */
  virtual void onHeadersSent(const HTTPMessage& headers,
                             bool codecWasReusable) {}

  virtual bool allTransactionsStarted() const = 0;

  void setNewTransactionPauseState(HTTPCodec::StreamID streamID);

  /**
   * Invoked when the transaction finishes sending a message and
   * appropriately shuts down reads and/or writes with respect to
   * downstream or upstream semantics.
   */
  void onEgressMessageFinished(HTTPTransaction* txn,
                               bool withRST = false);

  /**
   * Gets the next IOBuf to send (either writeBuf_ or new egress from
   * the priority queue), and sets cork appropriately
   */
  std::unique_ptr<folly::IOBuf> getNextToSend(bool* cork, bool* eom);

  void decrementTransactionCount(HTTPTransaction* txn,
                                 bool ingressEOM, bool egressEOM);

  size_t getCodecSendWindowSize() const;

  /**
   * Drains the current transactions and prevents new transactions from being
   * created on this session. If this is an upstream session and the
   * number of transactions reaches zero, this session will shutdown the
   * transport and delete itself. For downstream sessions, an explicit
   * call to dropConnection() or shutdownTransport() is required.
   */
  void drain();

  /**
   * Helper class to track write buffers until they have been fully written and
   * can be deleted.
   */
  class WriteSegment :
    public folly::AsyncTransportWrapper::WriteCallback {
   public:
    WriteSegment(HTTPSession* session, uint64_t length);

    void setCork(bool cork) {
      if (cork) {
        flags_ = flags_ | folly::WriteFlags::CORK;
      } else {
        unSet(flags_, folly::WriteFlags::CORK);
      }
    }

    void setEOR(bool eor) {
      if (eor) {
        flags_ = flags_ | folly::WriteFlags::EOR;
      } else {
        unSet(flags_, folly::WriteFlags::EOR);
      }
    }

    /**
     * Clear the session. This is used if the session
     * does not want to receive future notification about this segment.
     */
    void detach();

    folly::WriteFlags getFlags() {
      return flags_;
    }

    uint64_t getLength() const {
      return length_;
    }

    // AsyncTransport::WriteCallback methods
    void writeSuccess() noexcept override;
    void writeErr(
        size_t bytesWritten,
        const folly::AsyncSocketException&) noexcept override;

    folly::IntrusiveListHook listHook;
   private:

    /**
     * Unlink this segment from the list.
     */
    void remove();

    HTTPSession* session_;
    uint64_t length_;
    folly::WriteFlags flags_{
      folly::WriteFlags::NONE};
  };
  typedef folly::IntrusiveList<WriteSegment, &WriteSegment::listHook>
    WriteSegmentList;

  void readTimeoutExpired() noexcept;
  void writeTimeoutExpired() noexcept;
  void flowControlTimeoutExpired() noexcept;

  // AsyncTransportWrapper::ReadCallback methods
  void getReadBuffer(void** buf, size_t* bufSize) override;
  void readDataAvailable(size_t readSize) noexcept override;
  bool isBufferMovable() noexcept override;
  void readBufferAvailable(std::unique_ptr<folly::IOBuf>) noexcept override;
  void processReadData();
  void readEOF() noexcept override;
  void readErr(
      const folly::AsyncSocketException&) noexcept override;

  // HTTPCodec::Callback methods
  void onMessageBegin(HTTPCodec::StreamID streamID, HTTPMessage* msg) override;
  void onPushMessageBegin(HTTPCodec::StreamID streamID,
                          HTTPCodec::StreamID assocStreamID,
                          HTTPMessage* msg) override;
  void onHeadersComplete(HTTPCodec::StreamID streamID,
                         std::unique_ptr<HTTPMessage> msg) override;
  void onBody(HTTPCodec::StreamID streamID,
              std::unique_ptr<folly::IOBuf> chain, uint16_t padding) override;
  void onChunkHeader(HTTPCodec::StreamID stream, size_t length) override;
  void onChunkComplete(HTTPCodec::StreamID stream) override;
  void onTrailersComplete(HTTPCodec::StreamID streamID,
      std::unique_ptr<HTTPHeaders> trailers) override;
  void onMessageComplete(HTTPCodec::StreamID streamID, bool upgrade) override;
  void onError(HTTPCodec::StreamID streamID,
               const HTTPException& error, bool newTxn) override;
  void onAbort(HTTPCodec::StreamID streamID,
               ErrorCode code) override;
  void onGoaway(uint64_t lastGoodStreamID,
                ErrorCode code) override;
  void onPingRequest(uint64_t uniqueID) override;
  void onPingReply(uint64_t uniqueID) override;
  void onWindowUpdate(HTTPCodec::StreamID stream, uint32_t amount) override;
  void onSettings(const SettingsList& settings) override;
  uint32_t numOutgoingStreams() const override { return outgoingStreams_; }
  uint32_t numIncomingStreams() const override { return incomingStreams_; }

  // HTTPTransaction::Transport methods
  void pauseIngress(HTTPTransaction* txn) noexcept override;
  void resumeIngress(HTTPTransaction* txn) noexcept override;
  void transactionTimeout(HTTPTransaction* txn) noexcept override;
  void sendHeaders(HTTPTransaction* txn,
                   const HTTPMessage& headers,
                   HTTPHeaderSize* size) noexcept override;
  size_t sendBody(HTTPTransaction* txn, std::unique_ptr<folly::IOBuf>,
                  bool includeEOM) noexcept override;
  size_t sendChunkHeader(HTTPTransaction* txn,
                         size_t length) noexcept override;
  size_t sendChunkTerminator(HTTPTransaction* txn) noexcept override;
  size_t sendTrailers(HTTPTransaction* txn,
                      const HTTPHeaders& trailers) noexcept override;
  size_t sendEOM(HTTPTransaction* txn) noexcept override;
  size_t sendAbort(HTTPTransaction* txn,
                   ErrorCode statusCode) noexcept override;
  void detach(HTTPTransaction* txn) noexcept override;
  size_t sendWindowUpdate(HTTPTransaction* txn,
                          uint32_t bytes) noexcept override;
  void notifyPendingEgress() noexcept override;
  void notifyIngressBodyProcessed(uint32_t bytes) noexcept override;
  void notifyEgressBodyBuffered(int64_t bytes) noexcept override;
  HTTPTransaction* newPushedTransaction(
    HTTPCodec::StreamID assocStreamId,
    HTTPTransaction::PushHandler* handler) noexcept override;

 public:
  const folly::SocketAddress& getLocalAddress()
    const noexcept override;
  const folly::SocketAddress& getPeerAddress()
    const noexcept override;

  wangle::TransportInfo& getSetupTransportInfo() noexcept;
  const wangle::TransportInfo& getSetupTransportInfo() const noexcept override;
  bool getCurrentTransportInfo(wangle::TransportInfo* tinfo) override;
  virtual bool getCurrentTransportInfoWithoutUpdate(
      wangle::TransportInfo* tinfo) const;
  HTTPCodec& getCodec() noexcept {
    return *CHECK_NOTNULL(codec_.call());
  }
  const HTTPCodec& getCodec() const noexcept override {
    return *CHECK_NOTNULL(codec_.call());
  }

  void setByteEventTracker(std::unique_ptr<ByteEventTracker> byteEventTracker);
  ByteEventTracker* getByteEventTracker() { return byteEventTracker_.get(); }

  /**
   * If the connection is closed by remote end
   */
  bool connCloseByRemote() {
    auto sock = getTransport()->getUnderlyingTransport<folly::AsyncSocket>();
    if (sock) {
      return sock->isClosedByPeer();
    }
    return false;
  }

  /**
   * Turn on or off the persistentCork_ flag in the underlying AsyncSocket
   * @note this also immediately turn on or off TCP_CORK of the socket
   */
  void setPersistentCork(bool cork);

 protected:

  /**
   * Handle new messages from the codec and create a txn for the message.
   * @returns the created transaction.
   */
  HTTPTransaction* onMessageBeginImpl(HTTPCodec::StreamID streamID,
                                      HTTPCodec::StreamID assocStreamID,
                                      HTTPMessage* msg);

  // EventBase::LoopCallback methods
  void runLoopCallback() noexcept override;

  /**
   * Schedule a write to occur at the end of this event loop.
   */
  void scheduleWrite();

  /**
   * Update the size of the unwritten egress data and invoke
   * callbacks if the size has crossed the buffering limit.
   */
  void updateWriteCount();
  void updateWriteBufSize(int64_t delta);

  /**
   * Returns true iff egress should stop on this session.
   */
  bool egressLimitExceeded() const;

  /**
   * Tells us what would be the offset of the next byte to be
   * enqueued within the whole session.
   */
  inline uint64_t sessionByteOffset() {
    return bytesScheduled_ + writeBuf_.chainLength();
  }

  /**
   * Check whether the socket is shut down in both directions; if it is,
   * initiate the destruction of this HTTPSession.
   */
  void checkForShutdown();

  /**
   * Get the HTTPTransaction for the given transaction ID, or nullptr if that
   * transaction ID does not exist within this HTTPSession.
   */
  HTTPTransaction* findTransaction(HTTPCodec::StreamID streamID);

  /**
   * Create a new transaction.
   * @return pointer to the transaction on success, or else nullptr if it
   * already exists
   */
  HTTPTransaction* createTransaction(
    HTTPCodec::StreamID streamID,
    HTTPCodec::StreamID assocStreamID,
    http2::PriorityUpdate priority = http2::DefaultPriority);

  /** Invoked by WriteSegment on completion of a write. */
  void onWriteSuccess(uint64_t bytesWritten);

  /** Invoked by WriteSegment on write failure. */
  void onWriteError(size_t bytesWritten,
      const folly::AsyncSocketException& ex);

  /** Check whether to shut down the transport after a write completes. */
  void onWriteCompleted();

  /** Stop reading from the transport until resumeReads() is called */
  void pauseReads();

  /**
   * Send a session layer abort and shutdown the transport for reads and
   * writes.
   */
  void onSessionParseError(const HTTPException& error);

  /**
   * Send a transaction abort and leave the session and transport intact.
   */
  void onNewTransactionParseError(HTTPCodec::StreamID streamID,
                                  const HTTPException& error);

  /**
   * Install a direct response handler for the transaction based on the
   * error.
   */
  void handleErrorDirectly(HTTPTransaction* txn,
                           const HTTPException& error);

  /**
   * Unpause reading from the transport.
   * @note If any codec callbacks arrived while reads were paused,
   * they will be processed before network reads resume.
   */
  void resumeReads();

  /** Check whether the session has any writes in progress or upcoming */
  bool hasMoreWrites() const;

  /**
   * This function invokes a callback on all transactions. It is safe,
   * but runs in O(n*log n) and if the callback *adds* transactions,
   * they will not get the callback.
   */
  template<typename... Args1, typename... Args2>
  void invokeOnAllTransactions(void (HTTPTransaction::*fn)(Args1...),
                               Args2&&... args) {
    DestructorGuard g(this);
    std::vector<HTTPCodec::StreamID> ids;
    for (const auto& txn: transactions_) {
      ids.push_back(txn.first);
    }
    for (auto idit = ids.begin(); idit != ids.end() && !transactions_.empty();
         ++idit) {
      auto txn = findTransaction(*idit);
      if (txn != nullptr) {
        (txn->*fn)(std::forward<Args2>(args)...);
      }
    }
  }

  void resumeTransactions();

  /**
   * This function invokes a callback on all transactions. It is safe,
   * but runs in O(n*log n) and if the callback *adds* transactions,
   * they will not get the callback.
   */
  void errorOnAllTransactions(ProxygenError err, const std::string& errorMsg);

  void errorOnTransactionIds(const std::vector<HTTPCodec::StreamID>& ids,
                             ProxygenError err,
                             const std::string& extraErrorMsg = "");

  void errorOnTransactionId(HTTPCodec::StreamID id, HTTPException ex);

  void setCloseReason(ConnectionCloseReason reason) {
    if (closeReason_ == ConnectionCloseReason::kMAX_REASON) {
      closeReason_ = reason;
    }
  }

  /**
   * Returns true iff this session should shutdown at this time. Default
   * behavior is to not shutdown.
   */
  bool shouldShutdown() const;

  void drainImpl();

  /** Chain of ingress IOBufs */
  folly::IOBufQueue readBuf_{folly::IOBufQueue::cacheChainLength()};

  /** Queue of egress IOBufs */
  folly::IOBufQueue writeBuf_{folly::IOBufQueue::cacheChainLength()};

  /** Priority tree of transactions */
  HTTP2PriorityQueue txnEgressQueue_;

  std::map<HTTPCodec::StreamID, HTTPTransaction> transactions_;

  /** Count of transactions awaiting input */
  uint32_t liveTransactions_{0};

  /** Transaction sequence number */
  uint32_t transactionSeqNo_{0};

  /** Address of this end of the TCP connection */
  folly::SocketAddress localAddr_;

  /** Address of the remote end of the TCP connection */
  folly::SocketAddress peerAddr_;

  WriteSegmentList pendingWrites_;

  folly::AsyncTransportWrapper::UniquePtr sock_;

  HTTPSessionController* controller_{nullptr};

  HTTPCodecFilterChain codec_;

  InfoCallback* infoCallback_{nullptr};

  /**
   * The root cause reason this connection was closed.
   */
  ConnectionCloseReason closeReason_
    {ConnectionCloseReason::kMAX_REASON};

  WriteTimeout writeTimeout_;

  FlowControlTimeout flowControlTimeout_;

  folly::HHWheelTimer::SharedPtr transactionTimeouts_{nullptr};

  HTTPSessionStats* sessionStats_{nullptr};

  wangle::TransportInfo transportInfo_;

  /**
   * Connection level flow control for SPDY >= 3.1 and HTTP/2
   */
  FlowControlFilter* connFlowControl_{nullptr};

  /**
   * The maximum number of concurrent push transactions that can be supported
   * on this session
   */
  uint32_t maxConcurrentPushTransactions_{100};

  /**
   * The number of open push transactions
   */
  uint32_t pushedTxns_{0};

  /**
   * Bytes of egress data sent to the socket but not yet written
   * to the network.
   */
  uint64_t pendingWriteSize_{0};

  /**
   * The maximum number of concurrent transactions that this session may
   * create, as configured locally.
   */
  uint32_t maxConcurrentOutgoingStreamsConfig_{100};

  /**
   * The received setting for the maximum number of concurrent
   * transactions that this session may create. We may assume the
   * remote allows unlimited transactions until we get a SETTINGS frame,
   * but to be reasonable, assume the remote doesn't allow more than 100K
   * concurrent transactions on one connection.
   */
  uint32_t maxConcurrentOutgoingStreamsRemote_{100000};

  /**
   * The maximum number of concurrent transactions that this session's peer
   * may create.
   */
  uint32_t maxConcurrentIncomingStreams_{100};

  /**
   * The number concurrent transactions initiated by this session
   */
  uint32_t outgoingStreams_{0};

  /**
   * The maximum number concurrent transactions in the history of this session
   */
  uint32_t historicalMaxOutgoingStreams_{0};

  /**
   * The number of concurrent transactions initiated by this sessions's peer
   */
  uint32_t incomingStreams_{0};

  /**
   * Bytes of ingress data read from the socket, but not yet sent to a
   * transaction.
   */
  uint32_t pendingReadSize_{0};

  /**
   * Number of writes submitted to the transport for which we haven't yet
   * received completion or failure callbacks.
   */
  unsigned numActiveWrites_{0};

  /**
   * Number of bytes written so far.
   */
  uint64_t bytesWritten_{0};

  /**
   * Number of bytes scheduled so far.
   */
  uint64_t bytesScheduled_{0};

  /**
   * Number of HTTP Transcations created on this HTTP Session, including the
   * ongoing and finished ones. This helps the understanding of session re-usage
   */
  uint64_t numTxnServed_{0};

  /**
   * The net change this event loop in the amount of buffered bytes
   * for all this session's txns and socket write buffer.
   */
  int64_t pendingWriteSizeDelta_{0};

  /**
   * The latest time when this session became idle status
   */
  TimePoint latestActive_{};

  /**
   * The idle duration between latest two consecutive active status
   */
  std::chrono::seconds latestIdleDuration_{};

  /**
   * Container to hold the results of HTTP2PriorityQueue::nextEgress
   */
  HTTP2PriorityQueue::NextEgressResult nextEgressResults_;

  // Flow control settings
  size_t initialReceiveWindow_{0};
  size_t receiveStreamWindowSize_{0};
  size_t receiveSessionWindowSize_{0};

  enum SocketState {
    UNPAUSED = 0,
    PAUSED = 1,
    SHUTDOWN = 2,
  };

  SocketState reads_:2;
  SocketState writes_:2;

  /**
   * Indicates if the session is waiting for existing transactions to close.
   * Once all transactions close, the session will be deleted.
   */
  bool draining_:1;

  /**
   * Indicates whether an upgrade request has been received from the codec.
   */
  bool ingressUpgraded_:1;

  bool started_:1;

  bool writesDraining_:1;
  bool resetAfterDrainingWrites_:1;
  bool resetSocketOnShutdown_:1;
  // indicates a fatal error that prevents further ingress data processing
  bool ingressError_:1;
  bool inLoopCallback_:1;
  bool inResume_:1;
  bool pendingPause_:1;

  /**
   * Maximum number of ingress body bytes that can be buffered across all
   * transactions for this single session/connection.
   */
  static uint32_t kDefaultReadBufLimit;

  /**
   * Maximum number of bytes to egress per loop when there are > 1
   * transactions.  Otherwise defaults to kPendingWriteMax.
   */
  static uint64_t egressBodySizeLimit_;

  /**
   * Maximum number of bytes that can be buffered in sock_ before
   * this session will start applying backpressure to its transactions.
   */
  static uint32_t kPendingWriteMax;

 private:
  void onSetSendWindow(uint32_t windowSize);
  void onSetMaxInitiatedStreams(uint32_t maxTxns);

  void addLastByteEvent(HTTPTransaction* txn, uint64_t byteNo) noexcept;

  void addAckToLastByteEvent(HTTPTransaction* txn,
                             const ByteEvent& lastByteEvent);

  /**
   * Callback function from the flow control filter if the full window
   * becomes not full.
   */
  void onConnectionSendWindowOpen() override;
  void onConnectionSendWindowClosed() override;

  /**
   * Get the id of the stream we should ack in a graceful GOAWAY
   */
  HTTPCodec::StreamID getGracefulGoawayAck() const;

  /**
   * Invoked when the codec processes callbacks for a stream we are no
   * longer tracking.
   */
  void invalidStream(HTTPCodec::StreamID stream,
                     ErrorCode code = ErrorCode::_SPDY_INVALID_STREAM);

  http2::PriorityUpdate getMessagePriority(const HTTPMessage* msg);

  bool isConnWindowFull() const {
    return connFlowControl_ && connFlowControl_->getAvailableSend() == 0;
  }

  //ByteEventTracker::Callback functions
  void onPingReplyLatency(int64_t latency) noexcept override;
  uint64_t getAppBytesWritten() noexcept override;
  uint64_t getRawBytesWritten() noexcept override;
  void onDeleteAckEvent() override;

  std::unique_ptr<ByteEventTracker> byteEventTracker_{
    folly::make_unique<ByteEventTracker>(this)};
};

} // proxygen
