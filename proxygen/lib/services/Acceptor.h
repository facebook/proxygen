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

#include <folly/SocketAddress.h>
#include <folly/wangle/acceptor/Acceptor.h>
#include <thrift/lib/cpp/async/TAsyncSSLSocket.h>

// This is a hack until hphp third-party can be updated.

namespace proxygen {

typedef folly::TransportInfo TransportInfo;

class Acceptor {
  class AcceptorAdapter : public folly::Acceptor {
   public:
    AcceptorAdapter(const folly::ServerSocketConfig& config
                    , ::proxygen::Acceptor* acceptor)
      : folly::Acceptor(config)
      , acceptor_(acceptor) {}

   private:
    void onNewConnection(
      folly::AsyncSocket::UniquePtr s,
      const folly::SocketAddress* address,
      const std::string& nextProtocolName,
      const folly::TransportInfo& tinfo) {

      apache::thrift::async::TAsyncSocket::UniquePtr sock(
        dynamic_cast<apache::thrift::async::TAsyncSocket*>(
          s.release()));
      DCHECK(sock);

      acceptor_->onNewConnection(
        std::move(sock), address, nextProtocolName, tinfo);
    }

    folly::AsyncSocket::UniquePtr makeNewAsyncSocket(
      folly::EventBase* base, int fd) {
      return folly::AsyncSocket::UniquePtr(
        new apache::thrift::async::TAsyncSocket(base, fd));
    }

    folly::AsyncSSLSocket::UniquePtr makeNewAsyncSSLSocket(
      const std::shared_ptr<folly::SSLContext>& ctx,
      folly::EventBase* base, int fd) {
      return folly::AsyncSSLSocket::UniquePtr(
        new apache::thrift::async::TAsyncSSLSocket(ctx, base, fd));
    }

    virtual bool canAccept(const folly::SocketAddress& a) {
      return acceptor_->canAccept(a);
    }

    virtual void onConnectionsDrained() {
      acceptor_->onConnectionsDrained();
    }

    ::proxygen::Acceptor* acceptor_;
  };

 public:
  typedef folly::Acceptor::State State;
  explicit Acceptor(const folly::ServerSocketConfig& config)
    : acceptor_(config, this) {}

  uint32_t getNumConnections() const {
    return acceptor_.getNumConnections();
  }

  virtual void init(folly::AsyncServerSocket* serverSocket,
                    folly::EventBase* eventBase) {
    acceptor_.init(serverSocket, eventBase);
  }

  virtual void forceStop() {
    acceptor_.forceStop();
  }

 protected:
  virtual void onNewConnection(
    apache::thrift::async::TAsyncSocket::UniquePtr sock,
    const folly::SocketAddress* address,
    const std::string& nextProtocolName,
    const folly::TransportInfo& tinfo) = 0;

  virtual bool canAccept(const folly::SocketAddress&) = 0;
  virtual void onConnectionsDrained() = 0;
  virtual void addConnection(folly::wangle::ManagedConnection* connection) {
    acceptor_.addConnection(connection);
  }

 private:
  AcceptorAdapter acceptor_;
};

} // namespace
