/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Portability.h>
#include <folly/io/async/EventBase.h>
#include <gflags/gflags.h>
#include <iostream>
#include <thread>
#include <thrift/lib/cpp/async/TAsyncSSLSocket.h>
#include <thrift/lib/cpp/async/TAsyncSocket.h>
#include <vector>

using namespace std;

using apache::thrift::async::TAsyncSSLSocket;
using apache::thrift::async::TAsyncSocket;
using apache::thrift::transport::SSLContext;
using apache::thrift::transport::TTransportException;
using folly::EventBase;
using folly::SocketAddress;

DEFINE_int32(clients, 1, "Number of simulated SSL clients");
DEFINE_int32(threads, 1, "Number of threads to spread clients across");
DEFINE_int32(requests, 2, "Total number of requests per client");
DEFINE_int32(port, 9423, "Server port");
DEFINE_bool(sticky, false, "A given client sends all reqs to one "
            "(random) server");
DEFINE_bool(global, false, "All clients in a thread use the same SSL session");
DEFINE_bool(handshakes, false, "Force 100% handshakes");

string f_servers[10];
int f_num_servers = 0;
int tnum = 0;

class ClientRunner {
 public:

  ClientRunner(): reqs(0), hits(0), miss(0), num(tnum++) {}
  void run();

  int reqs;
  int hits;
  int miss;
  int num;
};

class SSLCacheClient : public TAsyncSocket::ConnectCallback,
                       public TAsyncSSLSocket::HandshakeCallback
{
private:
  EventBase* eventBase_;
  int currReq_;
  int serverIdx_;
  TAsyncSocket* socket_;
  TAsyncSSLSocket* sslSocket_;
  SSL_SESSION* session_;
  SSL_SESSION **pSess_;
  std::shared_ptr<SSLContext> ctx_;
  ClientRunner* cr_;

public:
  SSLCacheClient(EventBase* eventBase, SSL_SESSION **pSess, ClientRunner* cr);
  ~SSLCacheClient() {
    if (session_ && !FLAGS_global)
      SSL_SESSION_free(session_);
    if (socket_ != nullptr) {
      if (sslSocket_ != nullptr) {
        sslSocket_->destroy();
        sslSocket_ = nullptr;
      }
      socket_->destroy();
      socket_ = nullptr;
    }
  };

  void start();

  virtual void connectSuccess() noexcept;

  virtual void connectError(const TTransportException& ex)
    noexcept ;

  virtual void handshakeSuccess(TAsyncSSLSocket* sock) noexcept;

  virtual void handshakeError(
    TAsyncSSLSocket* sock,
    const apache::thrift::transport::TTransportException& ex) noexcept;

};

int
main(int argc, char* argv[])
{
  gflags::SetUsageMessage(std::string("\n\n"
"usage: sslcachetest [options] -c <clients> -t <threads> servers\n"
));
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  int reqs = 0;
  int hits = 0;
  int miss = 0;
  struct timeval start;
  struct timeval end;
  struct timeval result;

  srand((unsigned int)time(nullptr));

  for (int i = 1; i < argc; i++) {
    f_servers[f_num_servers++] = argv[i];
  }
  if (f_num_servers == 0) {
    cout << "require at least one server\n";
    return 1;
  }

  gettimeofday(&start, nullptr);
  if (FLAGS_threads == 1) {
    ClientRunner r;
    r.run();
    gettimeofday(&end, nullptr);
    reqs = r.reqs;
    hits = r.hits;
    miss = r.miss;
  }
  else {
    std::vector<ClientRunner> clients;
    std::vector<std::thread> threads;
    for (int t = 0; t < FLAGS_threads; t++) {
      threads.emplace_back([&] {
          clients[t].run();
        });
    }
    for (auto& thr: threads) {
      thr.join();
    }
    gettimeofday(&end, nullptr);

    for (const auto& client: clients) {
      reqs += client.reqs;
      hits += client.hits;
      miss += client.miss;
    }
  }

  timersub(&end, &start, &result);

  cout << "Requests: " << reqs << endl;
  cout << "Handshakes: " << miss << endl;
  cout << "Resumes: " << hits << endl;
  cout << "Runtime(ms): " << result.tv_sec << "." << result.tv_usec / 1000 <<
    endl;

  cout << "ops/sec: " << (reqs * 1.0) /
    ((double)result.tv_sec * 1.0 + (double)result.tv_usec / 1000000.0) << endl;

  return 0;
}

void
ClientRunner::run()
{
  EventBase eb;
  std::list<SSLCacheClient *> clients;
  SSL_SESSION* session = nullptr;

  for (int i = 0; i < FLAGS_clients; i++) {
    SSLCacheClient* c = new SSLCacheClient(&eb, &session, this);
    c->start();
    clients.push_back(c);
  }

  eb.loop();

  for (auto it = clients.begin(); it != clients.end(); it++) {
    delete* it;
  }

  reqs += hits + miss;
}

SSLCacheClient::SSLCacheClient(EventBase* eb,
                               SSL_SESSION **pSess,
                               ClientRunner* cr)
    : eventBase_(eb),
      currReq_(0),
      serverIdx_(0),
      socket_(nullptr),
      sslSocket_(nullptr),
      session_(nullptr),
      pSess_(pSess),
      cr_(cr)
{
  ctx_.reset(new SSLContext());
  ctx_->setOptions(SSL_OP_NO_TICKET);
}

void
SSLCacheClient::start()
{
  if (currReq_ >= FLAGS_requests) {
    cout << "+";
    return;
  }

  if (currReq_ == 0 || !FLAGS_sticky) {
    serverIdx_ = rand() % f_num_servers;
  }
  if (socket_ != nullptr) {
    if (sslSocket_ != nullptr) {
      sslSocket_->destroy();
      sslSocket_ = nullptr;
    }
    socket_->destroy();
    socket_ = nullptr;
  }
  socket_ = new TAsyncSocket(eventBase_);
  socket_->connect(this, f_servers[serverIdx_], (uint16_t)FLAGS_port);
}

void
SSLCacheClient::connectSuccess() noexcept
{
  sslSocket_ = new TAsyncSSLSocket(ctx_, eventBase_, socket_->detachFd(),
                                   false);

  if (!FLAGS_handshakes) {
    if (session_ != nullptr)
      sslSocket_->setSSLSession(session_);
    else if (FLAGS_global && pSess_ != nullptr)
      sslSocket_->setSSLSession(*pSess_);
  }
  sslSocket_->sslConnect(this);
}

void
SSLCacheClient::connectError(const TTransportException& ex)
  noexcept
{
  cout << "connectError: " << ex.what() << endl;
}

void
SSLCacheClient::handshakeSuccess(TAsyncSSLSocket* socket) noexcept
{
  if (sslSocket_->getSSLSessionReused()) {
    cr_->hits++;
  } else {
    cr_->miss++;
    if (session_ != nullptr) {
      SSL_SESSION_free(session_);
    }
    session_ = sslSocket_->getSSLSession();
    if (FLAGS_global && pSess_ != nullptr && *pSess_ == nullptr) {
      *pSess_ = session_;
    }
  }
  if ( ((cr_->hits + cr_->miss) % 100) == ((100 / FLAGS_threads) * cr_->num)) {
    cout << ".";
    cout.flush();
  }
  sslSocket_->closeNow();
  currReq_++;
  this->start();
}

void
SSLCacheClient::handshakeError(
  TAsyncSSLSocket* sock,
  const apache::thrift::transport::TTransportException& ex)
  noexcept
{
  cout << "handshakeError: " << ex.what() << endl;
}
