#include "CurlClient.h"

#include <iostream>
#include <fstream>

#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/SSLOptions.h>
#include <folly/portability/GFlags.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <proxygen/lib/http/codec/HTTP2Codec.h>

using namespace folly;
using namespace proxygen;
using namespace std;

DECLARE_int32(recv_window);

namespace CurlService {

CurlClient::CurlClient(EventBase* evb,
                       HTTPMethod httpMethod,
                       const URL& url,
                       const proxygen::URL* proxy,
                       const HTTPHeaders& headers,
                       const string& inputFilename,
                       bool h2c,
                       unsigned short httpMajor,
                       unsigned short httpMinor)
    : evb_(evb),
      httpMethod_(httpMethod),
      url_(url),
      inputFilename_(inputFilename),
      h2c_(h2c),
      httpMajor_(httpMajor),
      httpMinor_(httpMinor) {
  if (proxy != nullptr) {
    proxy_ = std::make_unique<URL>(proxy->getUrl());
  }

  headers.forEach([this] (const string& header, const string& val) {
      request_.getHeaders().add(header, val);
    });
}

HTTPHeaders CurlClient::parseHeaders(const std::string& headersString) {
  vector<StringPiece> headersList;
  HTTPHeaders headers;
  folly::split(",", headersString, headersList);
  for (const auto& headerPair: headersList) {
    vector<StringPiece> nv;
    folly::split('=', headerPair, nv);
    if (nv.size() > 0) {
      if (nv[0].empty()) {
        continue;
      }
      StringPiece value("");
      if (nv.size() > 1) {
        value = nv[1];
      } // trim anything else
      headers.add(nv[0], value);
    }
  }
  return headers;
}

void CurlClient::initializeSsl(const string& caPath,
                               const string& nextProtos,
                               const string& certPath,
                               const string& keyPath) {
  sslContext_ = std::make_shared<folly::SSLContext>();
  sslContext_->setOptions(SSL_OP_NO_COMPRESSION);
  sslContext_->setCipherList(folly::ssl::SSLCommonOptions::kCipherList);
  if (!caPath.empty()) {
    sslContext_->loadTrustedCertificates(caPath.c_str());
  }
  if (!certPath.empty() && !keyPath.empty()) {
    sslContext_->loadCertKeyPairFromFiles(certPath.c_str(), keyPath.c_str());
  }
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
  sendRequest(session->newTransaction(this));
  session->closeWhenIdle();
}

void CurlClient::sendRequest(HTTPTransaction* txn) {
  txn_ = txn;
  request_.setMethod(httpMethod_);
  request_.setHTTPVersion(httpMajor_, httpMinor_);
  if (proxy_) {
    request_.setURL(url_.getUrl());
  } else {
    request_.setURL(url_.makeRelativeURL());
  }
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
      cout.flush();
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
