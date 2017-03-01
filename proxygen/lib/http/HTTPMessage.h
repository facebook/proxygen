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

#include <array>
#include <boost/variant.hpp>
#include <folly/Conv.h>
#include <folly/Optional.h>
#include <folly/SocketAddress.h>
#include <folly/io/IOBufQueue.h>
#include <glog/logging.h>
#include <map>
#include <mutex>
#include <proxygen/lib/http/HTTPHeaderSize.h>
#include <proxygen/lib/http/HTTPHeaders.h>
#include <proxygen/lib/http/HTTPMethod.h>
#include <proxygen/lib/utils/ParseURL.h>
#include <proxygen/lib/utils/Time.h>
#include <string>

namespace proxygen {

/**
 * An HTTP request or response minus the body.
 *
 * Some of the methods on this class will assert if called from the wrong
 * context since they only make sense for a request or response. Make sure
 * you know what type of HTTPMessage this is before calling such methods.
 *
 * All header names stored in this class are case-insensitive.
 */
class HTTPMessage {
 public:

  HTTPMessage();
  ~HTTPMessage();
  HTTPMessage(const HTTPMessage& message);
  HTTPMessage& operator=(const HTTPMessage& message);

  /**
   * Is this a chunked message? (fpreq, fpresp)
   */
  void setIsChunked(bool chunked) { chunked_ = chunked; }
  bool getIsChunked() const { return chunked_; }

  /**
   * Is this an upgraded message? (fpreq, fpresp)
   */
  void setIsUpgraded(bool upgraded) { upgraded_ = upgraded; }
  bool getIsUpgraded() const { return upgraded_; }

  /**
   * Set/Get client address
   */
  void setClientAddress(const folly::SocketAddress& addr) {
    request().clientAddress_ = addr;
    request().clientIP_ = addr.getAddressStr();
    request().clientPort_ = folly::to<std::string>(addr.getPort());
  }

  const folly::SocketAddress& getClientAddress() const {
    return request().clientAddress_;
  }

  const std::string& getClientIP() const {
    return request().clientIP_;
  }

  const std::string& getClientPort() const {
    return request().clientPort_;
  }

  /**
   * Set/Get destination (vip) address
   */
  void setDstAddress(const folly::SocketAddress& addr) {
    dstAddress_ = addr;
    dstIP_ = addr.getAddressStr();
    dstPort_ = folly::to<std::string>(addr.getPort());
  }

  const folly::SocketAddress& getDstAddress() const {
    return dstAddress_;
  }

  const std::string& getDstIP() const {
    return dstIP_;
  }

  const std::string& getDstPort() const {
    return dstPort_;
  }

  /**
   * Set/Get the local IP address
   */
  template <typename T> // T = string
  void setLocalIp(T&& ip) {
    localIP_ = std::forward<T>(ip);
  }
  const std::string& getLocalIp() const {
    return localIP_;
  }

  /**
   * Access the method (fpreq)
   */
  void setMethod(HTTPMethod method);
  void setMethod(folly::StringPiece method);
  void rawSetMethod(const std::string& method) {
    setMethod(method);
  }

  /**
   * @Returns an HTTPMethod enum value representing the method if it is a
   * standard request method, or else "none" if it is an extension method
   * (fpreq)
   */
  boost::optional<HTTPMethod> getMethod() const;

  /**
   * @Returns a string representation of the request method (fpreq)
   */
  const std::string& getMethodString() const;

  /**
   * Access the URL component (fpreq)
   *
   * The <url> component from the initial "METHOD <url> HTTP/..." line. When
   * valid, this is a full URL, not just a path.
   */
  template <typename T> // T = string
  ParseURL setURL(T&& url) {
    VLOG(9) << "setURL: " << url;

    // Set the URL, path, and query string parameters
    ParseURL u(url);
    if (u.valid()) {
      VLOG(9) << "set path: " << u.path() << " query:" << u.query();
      request().path_ = u.path().str();
      request().query_ = u.query().str();
      unparseQueryParams();
    } else {
      VLOG(4) << "Error in parsing URL: " << url;
    }

    request().url_ = std::forward<T>(url);
    return u;
  }
  // The template function above doesn't work with char*,
  // so explicitly convert to a string first.
  void setURL(const char* url) {
    setURL(std::string(url));
  }
  const std::string& getURL() const {
    return request().url_;
  }
  void rawSetURL(const std::string& url) {
    setURL(url);
  }

  /**
   * Access the path component (fpreq)
   */
  const std::string& getPath() const {
    return request().path_;
  }

  /**
   * Access the query component (fpreq)
   */
  const std::string& getQueryString() const {
    return request().query_;
  }

  /**
   * Version constants
   */
  static const std::pair<uint8_t, uint8_t> kHTTPVersion10;
  static const std::pair<uint8_t, uint8_t> kHTTPVersion11;

  /**
   * Access the HTTP version number (fpreq, fpres)
   */
  void setHTTPVersion(uint8_t major, uint8_t minor);
  const std::pair<uint8_t, uint8_t>& getHTTPVersion() const;

  /**
   * Access the HTTP status message string (res)
   */
  template <typename T> // T = string
  void setStatusMessage(T&& msg) {
    response().statusMsg_ = std::forward<T>(msg);
  }
  const std::string& getStatusMessage() const {
    return response().statusMsg_;
  }
  void rawSetStatusMessage(std::string msg) {
    setStatusMessage(msg);
  }

  /**
   * Get/Set the HTTP version string (like "1.1").
   * XXX: Note we only support X.Y format while setting version.
   */
  const std::string& getVersionString() const {
    return versionStr_;
  }
  void setVersionString(const std::string& ver) {
    if (ver.size() != 3 ||
        ver[1] != '.' ||
        !isdigit(ver[0]) ||
        !isdigit(ver[2])) {
      return;
    }

    setHTTPVersion(ver[0] - '0', ver[2] - '0');
  }

  /**
   * Access the headers (fpreq, fpres)
   */
  HTTPHeaders& getHeaders() { return headers_; }
  const HTTPHeaders& getHeaders() const { return headers_; }

  /**
   * Access the trailers
   */
  HTTPHeaders* getTrailers() { return trailers_.get(); }
  const HTTPHeaders* getTrailers() const { return trailers_.get(); }

  /**
   * Set the trailers, replacing any that might already be present
   */
  void setTrailers(std::unique_ptr<HTTPHeaders>&& trailers) {
    trailers_ = std::move(trailers);
  }

  /**
   * Decrements Max-Forwards header, when present on OPTIONS or TRACE methods.
   *
   * Returns HTTP status code.
   */
  int processMaxForwards();

  /**
   * Returns true if the version of this message is HTTP/1.0
   */
  bool isHTTP1_0() const;

  /**
   * Returns true if the version of this message is HTTP/1.1
   */
  bool isHTTP1_1() const;

  /**
   * Returns true if this is a 1xx response.
   */
  bool is1xxResponse() const { return (getStatusCode() / 100) == 1; }

  /**
   * Formats the current time appropriately for a Date header
   */
  static std::string formatDateHeader();

  /**
   * Ensures this HTTPMessage contains a host header, adding a default one
   * with the destination address if necessary.
   */
  void ensureHostHeader();

  /**
   * Indicates if this request wants the connection to be kept-alive
   * (default true).  Not all codecs respect this option.
   */
  void setWantsKeepalive(bool wantsKeepaliveVal) {
    wantsKeepalive_ = wantsKeepaliveVal;
  }
  bool wantsKeepalive() const {
    return wantsKeepalive_;
  }

  /**
   * Returns true if trailers are allowed on this message.  Trailers
   * are not allowed on responses unless the client is expecting them.
   */
  bool trailersAllowed() const { return trailersAllowed_; }
  /**
   * Accessor to set whether trailers are allowed in the response
   */
  void setTrailersAllowed(bool trailersAllowedVal) {
    trailersAllowed_ = trailersAllowedVal;
  }

  /**
   * Returns true if this message has trailers that need to be serialized
   */
  bool hasTrailers() const {
    return trailersAllowed_ && trailers_ && trailers_->size() > 0;
  }

  /**
   * Access the status code (fpres)
   */
  void setStatusCode(uint16_t status);
  uint16_t getStatusCode() const;

  /**
   * Access the push status code
   */
  void setPushStatusCode(const uint16_t status);
  const std::string& getPushStatusStr() const;
  uint16_t getPushStatusCode() const;

  /**
   * Fill in the fields for a response message header that the server will
   * send directly to the client.
   *
   * @param version           HTTP version (major, minor)
   * @param statusCode        HTTP status code to respond with
   * @param msg               textual message to embed in "message" status field
   * @param contentLength     the length of the data to be written out through
   *                          this message
   */
  void constructDirectResponse(const std::pair<uint8_t, uint8_t>& version,
                               const int statusCode,
                               const std::string& statusMsg,
                               int contentLength = 0);

  /**
   * Fill in the fields for a response message header that the server will
   * send directly to the client. This function assumes the status code and
   * status message have already been set on this HTTPMessage object
   *
   * @param version           HTTP version (major, minor)
   * @param contentLength     the length of the data to be written out through
   *                          this message
   */
  void constructDirectResponse(const std::pair<uint8_t, uint8_t>& version,
                               int contentLength = 0);

  /**
   * Check if query parameter with the specified name exists.
   */
  bool hasQueryParam(const std::string& name) const;

  /**
   * Get the query parameter with the specified name.
   *
   * Returns a pointer to the query parameter value, or nullptr if there is no
   * parameter with the specified name.  The returned value is only valid as
   * long as this HTTPMessage object.
   */
  const std::string* getQueryParamPtr(const std::string& name) const;

  /**
   * Get the query parameter with the specified name.
   *
   * Returns a reference to the query parameter value, or
   * proxygen::empty_string if there is no parameter with the
   * specified name.  The returned value is only valid as long as this
   * HTTPMessage object.
   */
  const std::string& getQueryParam(const std::string& name) const;

  /**
   * Get the query parameter with the specified name as int.
   *
   * If the conversion fails, throws exception.
   */
  int getIntQueryParam(const std::string& name) const;

  /**
   * Get the query parameter with the specified name as int.
   *
   * Returns the query parameter if it can be parsed as int otherwise the
   * default value.
   */
  int getIntQueryParam(const std::string& name, int defval) const;

  /**
   * Get the query parameter with the specified name after percent decoding.
   *
   * Returns empty string if parameter is missing or folly::uriUnescape
   * query param
   */
  std::string getDecodedQueryParam(const std::string& name) const;

  /**
   * Get all the query parameters.
   *
   * Returns a reference to the query parameters map.  The returned
   * value is only valid as long as this
   * HTTPMessage object.
   */
  const std::map<std::string, std::string>& getQueryParams() const;

  /**
   * Set the query string to the specified value, and recreate the url_.
   *
   * Returns true if the query string was changed successfully.
   */
  bool setQueryString(const std::string& query);

  /**
   * Remove the query parameter with the specified name.
   *
   * Returns true if the query parameter was present and deleted.
   */
  bool removeQueryParam(const std::string& name);

  /**
   * Sets the query parameter with the specified name to the specified value.
   *
   * Returns true if the query parameter was successfully set.
   */
  bool setQueryParam(const std::string& name, const std::string& value);

  /**
   * Get the cookie with the specified name.
   *
   * Returns a StringPiece to the cookie value, or an empty StringPiece if
   * there is no cookie with the specified name.  The returned cookie is
   * only valid as long as the Cookie Header in HTTPMessage object exists.
   * Applications should make sure they call unparseCookies() when editing
   * the Cookie Header, so that the StringPiece references are cleared.
   */
  const folly::StringPiece getCookie(const std::string& name) const;

  /**
   * Print the message out.
   */
  void dumpMessage(int verbosity) const;

  /**
   * Print the message out, serializes through mutex
   * Used in shutdown path
   */
  void atomicDumpMessage(int verbosity) const;

  /**
   * Print the message out to logSink.
   */
  void dumpMessageToSink(google::LogSink* logSink) const;

  /**
   * Interact with headers that are defined to be per-hop.
   *
   * It is expected that during request processing, stripPerHopHeaders() will
   * be called before the message is proxied to the other connection.
   */
  void stripPerHopHeaders();

  const HTTPHeaders& getStrippedPerHopHeaders() const {
    return strippedPerHopHeaders_;
  }

  void setSecure(bool secure) { secure_ = secure; }
  bool isSecure() const { return secure_; }
  int getSecureVersion() const { return sslVersion_; }
  const char* getSecureCipher() const { return sslCipher_; }
  void setSecureInfo(int ver, const char* cipher) {
    // cipher is a static const char* provided and managed by openssl lib
    sslVersion_ = ver; sslCipher_ = cipher;
  }
  void setAdvancedProtocolString(const std::string& protocol) {
    protoStr_ = &protocol;
  }
  bool isAdvancedProto() const {
    return protoStr_ != nullptr;
  }
  const std::string* getAdvancedProtocolString() const {
    return protoStr_;
  }

  /* Setter and getter for the SPDY priority value (0 - 7).  When serialized
   * to SPDY/2, Codecs will collpase 0,1 -> 0, 2,3 -> 1, etc.
   *
   * Negative values of pri are interpreted much like negative array
   * indexes in python, so -1 will be the largest numerical priority
   * value for this SPDY version (i.e. 3 for SPDY/2 or 7 for SPDY/3),
   * -2 the second largest (i.e. 2 for SPDY/2 or 6 for SPDY/3).
   */
  const static int8_t kMaxPriority;

  static uint8_t normalizePriority(int8_t pri) {
    if (pri > kMaxPriority || pri < -kMaxPriority) {
      // outside [-7, 7] => highest priority
      return kMaxPriority;
    } else if (pri < 0) {
      return pri + kMaxPriority + 1;
    }
    return pri;
  }

  void setPriority(int8_t pri) {
    pri_ = normalizePriority(pri);
    h2Pri_ = boost::none;
  }
  uint8_t getPriority() const { return pri_; }

  typedef std::tuple<uint32_t, bool, uint8_t> HTTPPriority;

  boost::optional<HTTPPriority> getHTTP2Priority()
    const {
    return h2Pri_;
  }

  void setHTTP2Priority(HTTPPriority h2Pri) {
    h2Pri_ = h2Pri;
  }

  /**
   * get and setter for transaction sequence number
   */
  void setSeqNo(int32_t seqNo) { seqNo_ = seqNo; }
  int32_t getSeqNo() const { return seqNo_; }

  /**
   * getter and setter for size in serialized form
   */
  void setIngressHeaderSize(const HTTPHeaderSize& size) {
    size_ = size;
  }
  const HTTPHeaderSize& getIngressHeaderSize() const {
    return size_;
  }

  /**
   * Getter and setter for the time when the first byte of the message arrived
   */
  TimePoint getStartTime() const { return startTime_; }
  void setStartTime(const TimePoint& startTime) { startTime_ = startTime; }

  /**
   * Check if a particular token value is present in a header that consists of
   * a list of comma separated tokens.  (e.g., a header with a #rule
   * body as specified in the RFC 2616 BNF notation.)
   */
  bool checkForHeaderToken(const HTTPHeaderCode headerCode,
                           char const* token,
                           bool caseSensitive) const;

  /**
   * Forget about the parsed cookies.
   *
   * Ideally HTTPMessage should automatically forget about the current parsed
   * cookie state whenever a Cookie header is changed.  However, at the moment
   * callers have to explicitly call unparseCookies() after modifying the
   * cookie headers.
   */
  void unparseCookies();

  /**
   * Get the default reason string for a status code.
   *
   * This returns the reason string for the specified status code as specified
   * in RFC 2616.  For unknown status codes, the string "-" is returned.
   */
  static const char* getDefaultReason(uint16_t status);

  /**
   * Computes whether the state of this message is compatible with keepalive.
   * Changing the headers, version, etc can change the result.
   */
  bool computeKeepalive() const;

  /**
   * @returns true if this HTTPMessage represents an HTTP request
   */
  bool isRequest() const {
    return fields_.which() == 1;
  }

  /**
   * @returns true if this HTTPMessage represents an HTTP response
   */
  bool isResponse() const {
    return fields_.which() == 2;
  }

  /**
   * Assuming input contains
   * <name><valueDelim><value>(<pairDelim><name><valueDelim><value>),
   * invoke callback once with each name,value pair.
   */
  static void splitNameValuePieces(
      const std::string& input,
      char pairDelim,
      char valueDelim,
      std::function<void(folly::StringPiece, folly::StringPiece)> callback);

  static void splitNameValue(
      const std::string& input,
      char pairDelim,
      char valueDelim,
      std::function<void(std::string&&, std::string&&)> callback);

  /**
   * Form the URL from the individual components.
   * url -> {scheme}://{authority}{path}?{query}#{fragment}
   */
  static std::string createUrl(const folly::StringPiece scheme,
                               const folly::StringPiece authority,
                               const folly::StringPiece path,
                               const folly::StringPiece query,
                               const folly::StringPiece fragment);

  /**
   * Create a query string from the query parameters map
   * containing the name-value pairs.
   */
  static std::string createQueryString(
      const std::map<std::string, std::string>& params, uint32_t maxSize);

 protected:
  // Message start time, in msec since the epoch.
  TimePoint startTime_;
  int32_t seqNo_;

 private:

  void parseCookies() const;

  void parseQueryParams() const;
  void unparseQueryParams();

  /**
   * Trims whitespace from the beggining and end of the StringPiece.
   */
  static folly::StringPiece trim(folly::StringPiece sp);

  /** The 12 standard fields for HTTP messages. Use accessors.
   * An HTTPMessage is either a Request or Response.
   * Once an accessor for either is used, that fixes the type of HTTPMessage.
   * If an access is then used for the other type, a DCHECK will fail.
   */
  struct Request {
    folly::SocketAddress clientAddress_;
    std::string clientIP_;
    std::string clientPort_;
    mutable boost::variant<boost::blank, std::string, HTTPMethod> method_;
    std::string path_;
    std::string query_;
    std::string url_;

    uint16_t pushStatus_;
    std::string pushStatusStr_;
  };

  struct Response {
    uint16_t status_;
    std::string statusStr_;
    std::string statusMsg_;
  };

  folly::SocketAddress dstAddress_;
  std::string dstIP_;
  std::string dstPort_;

  std::string localIP_;
  std::string versionStr_;

  mutable boost::variant<boost::blank, Request, Response> fields_;

  Request& request() {
    DCHECK(fields_.which() == 0 || fields_.which() == 1) << fields_.which();
    if (fields_.which() == 0) {
      fields_ = Request();
    }

    return boost::get<Request>(fields_);
  }

  const Request& request() const {
    DCHECK(fields_.which() == 0 || fields_.which() == 1) << fields_.which();
    if (fields_.which() == 0) {
      fields_ = Request();
    }

    return boost::get<const Request>(fields_);
  }

  Response& response() {
    DCHECK(fields_.which() == 0 || fields_.which() == 2) << fields_.which();
    if (fields_.which() == 0) {
      fields_ = Response();
    }

    return boost::get<Response>(fields_);
  }

  const Response& response() const {
    DCHECK(fields_.which() == 0 || fields_.which() == 2) << fields_.which();
    if (fields_.which() == 0) {
      fields_ = Response();
    }

    return boost::get<const Response>(fields_);
  }

  /*
   * Cookies and query parameters
   * These are mutable since we parse them lazily in getCookie() and
   * getQueryParam()
   */
  mutable std::map<folly::StringPiece, folly::StringPiece> cookies_;
  // TODO: use StringPiece for queryParams_ and delete splitNameValue()
  mutable std::map<std::string, std::string> queryParams_;

  std::pair<uint8_t, uint8_t> version_;
  HTTPHeaders headers_;
  HTTPHeaders strippedPerHopHeaders_;
  HTTPHeaderSize size_;
  std::unique_ptr<HTTPHeaders> trailers_;

  int sslVersion_;
  const char* sslCipher_;
  const std::string* protoStr_;
  uint8_t pri_;
  boost::optional<HTTPPriority> h2Pri_;

  mutable bool parsedCookies_:1;
  mutable bool parsedQueryParams_:1;
  bool chunked_:1;
  bool upgraded_:1;
  bool wantsKeepalive_:1;
  bool trailersAllowed_:1;

  // Whether the message is received in HTTPS.
  bool secure_:1;

  // used by atomicDumpMessage
  static std::mutex mutexDump_;
};

/**
 * Returns a std::string that has the control characters removed from the
 * input string.
 */
template<typename Str>
std::string stripCntrlChars(const Str& str) {
  std::string res;
  res.reserve(str.length());
  for (size_t i = 0; i < str.size(); ++i) {
    if (!(str[i] <= 0x1F || str[i] == 0x7F)) {
      res += str[i];
    }
  }
  return res;
}

} // proxygen
