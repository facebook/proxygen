#include "CurlClient.h"

#include <iostream>
#include <fstream>

#include <folly/FileUtil.h>
#include <folly/String.h>
#include <gflags/gflags.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <proxygen/lib/http/codec/HTTP2Codec.h>
#include <wangle/ssl/SSLContextConfig.h>

using namespace folly;
using namespace proxygen;
using namespace std;

DECLARE_int32(recv_window);

namespace CurlService {

CurlClient::CurlClient(EventBase* evb, HTTPMethod httpMethod, const URL& url,
                       const HTTPHeaders& headers, const string& inputFilename,
                       bool h2c):
    evb_(evb), httpMethod_(httpMethod), url_(url),
    inputFilename_(inputFilename), h2c_(h2c) {
  headers.forEach([this] (const string& header, const string& val) {
      request_.getHeaders().add(header, val);
    });
}

CurlClient::~CurlClient() {
}


void CurlClient::initializeSsl(const string& certPath,
                               const string& nextProtos) {
  sslContext_ = std::make_shared<folly::SSLContext>();
  sslContext_->setOptions(SSL_OP_NO_COMPRESSION);
  wangle::SSLContextConfig config;
  sslContext_->ciphers(config.sslCiphers);
  sslContext_->loadTrustedCertificates(certPath.c_str());
  list<string> nextProtoList;
  folly::splitTo<string>(',', nextProtos, std::inserter(nextProtoList,
                                                        nextProtoList.begin()));
  sslContext_->setAdvertisedNextProtocols(nextProtoList);
  h2c_ = false;
}


void CurlClient::sslHandshakeFollowup(HTTPUpstreamSession* session) noexcept {
  AsyncSSLSocket* sslSocket = dynamic_cast<AsyncSSLSocket*>(
    session->getTransport());

  const unsigned char* nextProto = nullptr;
  unsigned nextProtoLength = 0;
  sslSocket->getSelectedNextProtocol(&nextProto, &nextProtoLength);
  if (nextProto) {
    VLOG(1) << "Client selected next protocol " <<
      string((const char*)nextProto, nextProtoLength);
  } else {
    VLOG(1) << "Client did not select a next protocol";
  }

  // Note: This ssl session can be used by defining a member and setting
  // something like sslSession_ = sslSocket->getSSLSession() and then
  // passing it to the connector::connectSSL() method
}

void CurlClient::setFlowControlSettings(int32_t recvWindow) {
  recvWindow_ = recvWindow;
}

void CurlClient::connectSuccess(HTTPUpstreamSession* session) {

  if (url_.isSecure()) {
    sslHandshakeFollowup(session);
  }

  session->setFlowControl(recvWindow_, recvWindow_, recvWindow_);

  txn_ = session->newTransaction(this);
  request_.setMethod(httpMethod_);
  request_.setHTTPVersion(1, 1);
  request_.setURL(url_.makeRelativeURL());
  request_.setSecure(url_.isSecure());
  if (h2c_) {
    HTTP2Codec::requestUpgrade(request_);
  }

  if (!request_.getHeaders().getNumberOfValues(HTTP_HEADER_USER_AGENT)) {
    request_.getHeaders().add(HTTP_HEADER_USER_AGENT, "proxygen_curl");
  }
  if (!request_.getHeaders().getNumberOfValues(HTTP_HEADER_HOST)) {
    request_.getHeaders().add(HTTP_HEADER_HOST, url_.getHostAndPort());
  }
  if (!request_.getHeaders().getNumberOfValues(HTTP_HEADER_ACCEPT)) {
    request_.getHeaders().add("Accept", "*/*");
  }
  if (loggingEnabled_) {
    request_.dumpMessage(4);
  }

  txn_->sendHeaders(request_);

  unique_ptr<IOBuf> buf;
  if (httpMethod_ == HTTPMethod::POST) {

    const uint16_t kReadSize = 4096;
    ifstream inputFile(inputFilename_, ios::in | ios::binary);

    // Reading from the file by chunks
    // Important note: It's pretty bad to call a blocking i/o function like
    // ifstream::read() in an eventloop - but for the sake of this simple
    // example, we'll do it.
    // An alternative would be to put this into some folly::AsyncReader
    // object.
    while (inputFile.good()) {
      buf = IOBuf::createCombined(kReadSize);
      inputFile.read((char*)buf->writableData(), kReadSize);
      buf->append(inputFile.gcount());
      txn_->sendBody(move(buf));
    }
  }

  // note that sendBody() is called only for POST. It's fine not to call it
  // at all.

  txn_->sendEOM();
  session->closeWhenIdle();
}

void CurlClient::connectError(const folly::AsyncSocketException& ex) {
  LOG_IF(ERROR, loggingEnabled_) << "Coudln't connect to "
                                 << url_.getHostAndPort() << ":" << ex.what();
}

void CurlClient::setTransaction(HTTPTransaction*) noexcept {
}

void CurlClient::detachTransaction() noexcept {
}

void CurlClient::onHeadersComplete(unique_ptr<HTTPMessage> msg) noexcept {
  response_ = std::move(msg);
  if (!loggingEnabled_) {
    return;
  }
  cout << response_->getStatusCode() << " "
       << response_->getStatusMessage() << endl;
  response_->getHeaders().forEach([&](const string& header, const string& val) {
    cout << header << ": " << val << endl;
  });
}

void CurlClient::onBody(std::unique_ptr<folly::IOBuf> chain) noexcept {
  if (!loggingEnabled_) {
    return;
  }
  if (chain) {
    const IOBuf* p = chain.get();
    do {
      cout.write((const char*)p->data(), p->length());
      p = p->next();
    } while (p != chain.get());
  }
}

void CurlClient::onTrailers(std::unique_ptr<HTTPHeaders>) noexcept {
  LOG_IF(INFO, loggingEnabled_) << "Discarding trailers";
}

void CurlClient::onEOM() noexcept {
  LOG_IF(INFO, loggingEnabled_) << "Got EOM";
}

void CurlClient::onUpgrade(UpgradeProtocol) noexcept {
  LOG_IF(INFO, loggingEnabled_) << "Discarding upgrade protocol";
}

void CurlClient::onError(const HTTPException& error) noexcept {
  LOG_IF(ERROR, loggingEnabled_) << "An error occurred: " << error.what();
}

void CurlClient::onEgressPaused() noexcept {
  LOG_IF(INFO, loggingEnabled_) << "Egress paused";
}

void CurlClient::onEgressResumed() noexcept {
  LOG_IF(INFO, loggingEnabled_) << "Egress resumed";
}

const string& CurlClient::getServerName() const {
  const string& res = request_.getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST);
  if (res.empty()) {
    return url_.getHost();
  }
  return res;
}

}  // namespace CurlService
