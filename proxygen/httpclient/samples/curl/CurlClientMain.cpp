/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 *
 *  This is a higly simplified example of an HTTP client. This can be heavily
 *  extended using mechanisms like redirects, transfer encoding, connection
 *  pooling, etc.
 *
 */
#include <gflags/gflags.h>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>
#include <folly/SocketAddress.h>
#include <proxygen/lib/http/HTTPConnector.h>
#include "CurlClient.h"

using namespace CurlService;
using namespace folly;
using namespace proxygen;
using std::vector;
using std::string;

DEFINE_string(http_method, "GET",
    "HTTP method to use. GET or POST are supported");
DEFINE_string(url, "https://github.com/facebook/proxygen",
    "URL to perform the HTTP method against");
DEFINE_string(input_filename, "",
    "Filename to read from for POST requests");
DEFINE_int32(http_client_connect_timeout, 1000,
    "connect timeout in milliseconds");
DEFINE_string(cert_path, "/etc/ssl/certs/ca-certificates.crt",
    "Path to trusted cert to authenticate with");  // default for Ubuntu 14.04
DEFINE_string(next_protos, "h2,h2-14,spdy/3.1,spdy/3,http/1.1",
    "Next protocol string for NPN/ALPN");
DEFINE_string(plaintext_proto, "", "plaintext protocol");
DEFINE_int32(recv_window, 65536, "Flow control receive window for h2/spdy");
DEFINE_bool(h2c, true, "Attempt HTTP/1.1 -> HTTP/2 upgrade");
DEFINE_string(headers, "", "List of N=V headers separated by ,");

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  EventBase evb;
  URL url(FLAGS_url);

  if (FLAGS_http_method != "GET" && FLAGS_http_method != "POST") {
    LOG(ERROR) << "http_method must be either GET or POST";
    return EXIT_FAILURE;
  }

  HTTPMethod httpMethod = *stringToMethod(FLAGS_http_method);
  if (httpMethod == HTTPMethod::POST) {
    try {
      File(FLAGS_input_filename);
    } catch (const std::system_error& se) {
      LOG(ERROR) << "Couldn't open file for POST method";
      LOG(ERROR) << se.what();
      return EXIT_FAILURE;
    }
  }

  vector<StringPiece> headerList;
  HTTPHeaders headers;
  folly::split("|", FLAGS_headers, headerList);
  for (const auto& headerPair: headerList) {
    vector<StringPiece> nv;
    folly::split(':', headerPair, nv);
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

  CurlClient curlClient(&evb, httpMethod, url, headers,
                        FLAGS_input_filename, FLAGS_h2c);
  curlClient.setFlowControlSettings(FLAGS_recv_window);

  SocketAddress addr(url.getHost(), url.getPort(), true);
  LOG(INFO) << "Trying to connect to " << addr;

  // Note: HHWheelTimer is a large object and should be created at most
  // once per thread
  HHWheelTimer::UniquePtr timer{HHWheelTimer::newTimer(
      &evb,
      std::chrono::milliseconds(HHWheelTimer::DEFAULT_TICK_INTERVAL),
      AsyncTimeout::InternalEnum::NORMAL,
      std::chrono::milliseconds(5000))};
  HTTPConnector connector(&curlClient, timer.get());
  if (!FLAGS_plaintext_proto.empty()) {
    connector.setPlaintextProtocol(FLAGS_plaintext_proto);
  }
  static const AsyncSocket::OptionMap opts{{{SOL_SOCKET, SO_REUSEADDR}, 1}};

  if (url.isSecure()) {
    curlClient.initializeSsl(FLAGS_cert_path, FLAGS_next_protos);
    connector.connectSSL(
      &evb, addr, curlClient.getSSLContext(), nullptr,
      std::chrono::milliseconds(FLAGS_http_client_connect_timeout), opts,
      folly::AsyncSocket::anyAddress(), curlClient.getServerName());
  } else {
    connector.connect(&evb, addr,
        std::chrono::milliseconds(FLAGS_http_client_connect_timeout), opts);
  }

  evb.loop();

  return EXIT_SUCCESS;
}
