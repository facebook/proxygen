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

#include <folly/io/async/SSLContext.h>
#include <proxygen/lib/http/session/HTTPSession.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>

namespace proxygen {

class HTTPSessionStats;
class SPDYStats;
class HTTPUpstreamSessionController;

class HTTPUpstreamSession final: public HTTPSession {
 public:
  // The interfaces defined below update the virtual stream based priority
  // scheme from the current system which allows only strict priorities to a
  // flexible system allowing an arbitrary tree of virtual streams, subject only
  // to the limitations in the HTTP/2 specification. An arbitrary prioritization
  // scheme can be implemented by constructing virtual streams corresponding to
  // the desired prioritization and attaching new streams as dependencies of the
  // appropriate virtual stream.
  //
  // The user must define a map from an opaque integer priority level to an
  // HTTP/2 priority corresponding to the virtual stream. This map is
  // implemented by the user in a class that extends
  // HTTPUpstreamSession::PriorityMapFactory. A shared pointer to this class is
  // passed into the constructor of HTTPUpstreamSession. This method will send
  // the virtual streams and return a unique pointer to a class that extends
  // HTTPUpstreamSession::PriorityAdapter. This class implements the map between
  // the user defined priority level and the HTTP/2 priority level.
  //
  // When the session is started, the createVirtualStreams method of
  // PriorityMapFactory is called by HTTPUpstreamSession::startNow. The returned
  // pointer to the PriorityAdapter map is cached in HTTPUpstreamSession. The
  // HTTP/2 priority that should be used for a new stream dependent on a virtual
  // stream corresponding to a given priority level is then retrieved by calling
  // the HTTPUpstreamSession::getHTTPPriority(uint8_t level) method.
  //
  // The prior strict priority system has been left in place for now, but if
  // both maxLevels and PriorityMapFactory are passed into the
  // HTTPUpstreamSession constructor, the maxLevels parameter will be ignored.

  // Implments a map from generic priority level to HTTP/2 priority.
  class PriorityAdapter {
   public:
    virtual folly::Optional<const HTTPMessage::HTTPPriority>
        getHTTPPriority(uint8_t level) = 0;
    virtual ~PriorityAdapter() = default;
  };

  class PriorityMapFactory {
   public:
    // Creates the map implemented by PriorityAdapter, sends the corresponding
    // virtual stream on the given session, and retuns the map.
    virtual std::unique_ptr<PriorityAdapter> createVirtualStreams(
       HTTPPriorityMapFactoryProvider* session) const = 0;
    virtual ~PriorityMapFactory() = default;
  };

  /**
   * @param sock           An open socket on which any applicable TLS
   *                         handshaking has been completed already.
   * @param localAddr      Address and port of the local end of the socket.
   * @param peerAddr       Address and port of the remote end of the socket.
   * @param codec          A codec with which to parse/generate messages in
   *                         whatever HTTP-like wire format this session needs.
   * @param maxVirtualPri  Number of virtual priority nodes to represent fixed
   *                         priority levels.
   */
  HTTPUpstreamSession(
      const WheelTimerInstance& timeout,
      folly::AsyncTransportWrapper::UniquePtr&& sock,
      const folly::SocketAddress& localAddr,
      const folly::SocketAddress& peerAddr,
      std::unique_ptr<HTTPCodec> codec,
      const wangle::TransportInfo& tinfo,
      InfoCallback* infoCallback,
      uint8_t maxVirtualPri = 0,
      std::shared_ptr<const PriorityMapFactory> priorityMapFactory =
          std::shared_ptr<const PriorityMapFactory>()) :
    HTTPSession(
        timeout,
        std::move(sock),
        localAddr,
        peerAddr,
        nullptr,
        std::move(codec),
        tinfo,
        infoCallback),
    maxVirtualPriorityLevel_(priorityMapFactory ? 0 : maxVirtualPri),
    priorityMapFactory_(priorityMapFactory) {
    if (sock_) {
      auto asyncSocket = sock_->getUnderlyingTransport<folly::AsyncSocket>();
      if (asyncSocket) {
        asyncSocket->setBufferCallback(this);
      }
    }
    CHECK_EQ(codec_->getTransportDirection(), TransportDirection::UPSTREAM);
  }

  // uses folly::HHWheelTimer instance which is used on client side & thrift
  HTTPUpstreamSession(
      folly::HHWheelTimer* timeout,
      folly::AsyncTransportWrapper::UniquePtr&& sock,
      const folly::SocketAddress& localAddr,
      const folly::SocketAddress& peerAddr,
      std::unique_ptr<HTTPCodec> codec,
      const wangle::TransportInfo& tinfo,
      InfoCallback* infoCallback,
      uint8_t maxVirtualPri = 0,
      std::shared_ptr<const PriorityMapFactory> priorityMapFactory =
          std::shared_ptr<const PriorityMapFactory>()) :
    HTTPUpstreamSession(
        WheelTimerInstance(timeout),
        std::move(sock),
        localAddr,
        peerAddr,
        std::move(codec),
        tinfo,
        infoCallback,
        maxVirtualPri,
        priorityMapFactory) {
  }

  using FilterIteratorFn = std::function<void(HTTPCodecFilter*)>;

  bool isDetachable(bool checkSocket=true) const;

  void attachThreadLocals(folly::EventBase* eventBase,
                          folly::SSLContextPtr sslContext,
                          const WheelTimerInstance& timeout,
                          HTTPSessionStats* stats,
                          FilterIteratorFn fn,
                          HeaderCodec::Stats* headerCodecStats,
                          HTTPUpstreamSessionController* controller);

  void detachThreadLocals(bool detachSSLContext=false);

  void startNow() override;

  /**
   * Creates a new transaction on this upstream session. Invoking this function
   * also has the side-affect of starting reads after this event loop completes.
   *
   * @param handler The request handler to attach to this transaction. It must
   *                not be null.
   */
  HTTPTransaction* newTransaction(HTTPTransaction::Handler* handler);

  /**
   * Returns true if this session has no open transactions and the underlying
   * transport can be used again in a new request.
   */
  bool isReusable() const;

  /**
   * Returns true if the session is shutting down
   */
  bool isClosing() const;

  /**
   * Drains the current transactions and prevents new transactions from being
   * created on this session. When the number of transactions reaches zero, this
   * session will shutdown the transport and delete itself.
   */
  void drain() override {
    HTTPSession::drain();
  }

 virtual folly::Optional<const HTTPMessage::HTTPPriority> getHTTPPriority(
     uint8_t level) override {
   if (!priorityAdapter_) {
     return HTTPSession::getHTTPPriority(level);
   }
   return priorityAdapter_->getHTTPPriority(level);
 }

 private:
  ~HTTPUpstreamSession() override;

  /**
   * Called by onHeadersComplete(). Currently a no-op for upstream.
   */
  void setupOnHeadersComplete(
      HTTPTransaction* /* txn */, HTTPMessage* /* msg */) override {}

  /**
   * Called by processParseError() if the transaction has no handler.
   */
  HTTPTransaction::Handler* getParseErrorHandler(
    HTTPTransaction* txn, const HTTPException& error) override;

  /**
   * Called by transactionTimeout() if the transaction has no handler.
   */
  HTTPTransaction::Handler* getTransactionTimeoutHandler(
    HTTPTransaction* txn) override;

  bool allTransactionsStarted() const override;

  bool onNativeProtocolUpgrade(
    HTTPCodec::StreamID streamID, CodecProtocol protocol,
    const std::string& protocolString,
    HTTPMessage& msg) override;

  uint8_t maxVirtualPriorityLevel_{0};

  std::shared_ptr<const PriorityMapFactory> priorityMapFactory_;
  std::unique_ptr<PriorityAdapter> priorityAdapter_;
};

} // proxygen
