#include "CurlClient.h"

#include <errno.h>
#include <sys/stat.h>

#include <fstream>

#include <folly/FileUtil.h>
#include <folly/String.h>
#include <proxygen/lib/http/HTTPMessage.h>
#include <proxygen/lib/http/session/HTTPUpstreamSession.h>
#include <proxygen/lib/ssl/SSLContextConfig.h>

using namespace folly;
using namespace proxygen;
using namespace std;

DECLARE_int32(recv_window);

namespace CurlService {

CurlClient::CurlClient(EventBase* evb, HTTPMethod httpMethod, const URL& url,
                       const HTTPHeaders& headers, const string& inputFilename):
    evb_(evb), httpMethod_(httpMethod), url_(url),
    inputFilename_(inputFilename) {
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
  SSLContextConfig config;
  sslContext_->ciphers(config.sslCiphers);
  sslContext_->loadTrustedCertificates(certPath.c_str());
  list<string> nextProtoList;
  folly::splitTo<string>(',', nextProtos, std::inserter(nextProtoList,
                                                        nextProtoList.begin()));
  sslContext_->setAdvertisedNextProtocols(nextProtoList);
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


void CurlClient::connectSuccess(HTTPUpstreamSession* session) {

  if (url_.isSecure()) {
    sslHandshakeFollowup(session);
  }

  session->setFlowControl(FLAGS_recv_window, FLAGS_recv_window,
                          FLAGS_recv_window);

  txn_ = session->newTransaction(this);
  request_.setMethod(httpMethod_);
  request_.setHTTPVersion(1, 1);
  request_.setURL(url_.makeRelativeURL());
  request_.setSecure(url_.isSecure());

  if (!request_.getHeaders().getNumberOfValues(HTTP_HEADER_USER_AGENT)) {
    request_.getHeaders().add(HTTP_HEADER_USER_AGENT, "proxygen_curl");
  }
  if (!request_.getHeaders().getNumberOfValues(HTTP_HEADER_HOST)) {
    request_.getHeaders().add(HTTP_HEADER_HOST, url_.getHostAndPort());
  }
  if (!request_.getHeaders().getNumberOfValues(HTTP_HEADER_ACCEPT)) {
    request_.getHeaders().add("Accept", "*/*");
  }
  request_.dumpMessage(4);

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
  LOG(ERROR) << "Coudln't connect to " << url_.getHostAndPort() << ":" <<
    ex.what();
}

void CurlClient::setTransaction(HTTPTransaction*) noexcept {
}

void CurlClient::detachTransaction() noexcept {
}

void CurlClient::onHeadersComplete(unique_ptr<HTTPMessage> msg) noexcept {
  msg->getHeaders().forEach([&] (const string& header, const string& val) {
      cout << header << ": " << val << endl;
      });
}

void CurlClient::onBody(std::unique_ptr<folly::IOBuf> chain) noexcept {
  if (chain) {
    const IOBuf* p = chain.get();
    do {
      cout.write((const char*)p->data(), p->length());
      p = p->next();
    } while (p->next() != chain.get());
  }
}

void CurlClient::onTrailers(std::unique_ptr<HTTPHeaders>) noexcept {
  LOG(INFO) << "Discarding trailers";
}

void CurlClient::onEOM() noexcept {
  LOG(INFO) << "Got EOM";
}

void CurlClient::onUpgrade(UpgradeProtocol) noexcept {
  LOG(INFO) << "Discarding upgrade protocol";
}

void CurlClient::onError(const HTTPException& error) noexcept {
  LOG(ERROR) << "An error occurred: " << error.what();
}

void CurlClient::onEgressPaused() noexcept {
  LOG(INFO) << "Egress paused";
}

void CurlClient::onEgressResumed() noexcept {
  LOG(INFO) << "Egress resumed";
}

const string& CurlClient::getServerName() const {
  const string& res = request_.getHeaders().getSingleOrEmpty(HTTP_HEADER_HOST);
  if (res.empty()) {
    return url_.getHost();
  }
  return res;
}

}  // namespace CurlService
