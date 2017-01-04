/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/HTTPMessage.h>

#include <boost/algorithm/string.hpp>
#include <folly/Format.h>
#include <folly/Range.h>
#include <folly/SingletonThreadLocal.h>
#include <string>
#include <vector>

using folly::IOBuf;
using folly::Optional;
using folly::StringPiece;
using std::pair;
using std::string;
using std::unique_ptr;

namespace {
const string kHeaderStr_ = "header.";
const string kQueryStr_ = "query.";
const string kCookieStr = "cookie.";

/**
 * Create a C locale once and pass it to all the boost string methods
 * that would otherwise create and destruct a temporary locale object
 * per call.  (Performance profiling showed that we were spending
 * approximately 1% of our total CPU time on temporary locale objects.)
 */
std::locale defaultLocale;
}

namespace proxygen {

const int8_t HTTPMessage::kMaxPriority = 7;
std::mutex HTTPMessage::mutexDump_;

const pair<uint8_t, uint8_t> HTTPMessage::kHTTPVersion10(1, 0);
const pair<uint8_t, uint8_t> HTTPMessage::kHTTPVersion11(1, 1);

void HTTPMessage::stripPerHopHeaders() {
  // Some code paths end up recyling a single HTTPMessage instance for multiple
  // requests, and adding their own per-hop headers each time. In that case, we
  // don't want to accumulate these headers.
  strippedPerHopHeaders_.removeAll();

  if (!trailersAllowed_) {
    // Because stripPerHopHeaders can be called multiple times, don't
    // let subsequent instances clear this flag
    trailersAllowed_ = checkForHeaderToken(HTTP_HEADER_TE, "trailers", false);
  }

  headers_.stripPerHopHeaders(strippedPerHopHeaders_);
}

HTTPMessage::HTTPMessage() :
    startTime_(getCurrentTime()),
    seqNo_(-1),
    localIP_(),
    versionStr_("1.0"),
    fields_(),
    version_(1,0),
    sslVersion_(0), sslCipher_(nullptr), protoStr_(nullptr), pri_(0),
    parsedCookies_(false), parsedQueryParams_(false),
    chunked_(false), upgraded_(false), wantsKeepalive_(true),
    trailersAllowed_(false), secure_(false) {
}

HTTPMessage::~HTTPMessage() {
}

HTTPMessage::HTTPMessage(const HTTPMessage& message) :
    startTime_(message.startTime_),
    seqNo_(message.seqNo_),
    dstAddress_(message.dstAddress_),
    dstIP_(message.dstIP_),
    dstPort_(message.dstPort_),
    localIP_(message.localIP_),
    versionStr_(message.versionStr_),
    fields_(message.fields_),
    cookies_(message.cookies_),
    queryParams_(message.queryParams_),
    version_(message.version_),
    headers_(message.headers_),
    strippedPerHopHeaders_(message.headers_),
    sslVersion_(message.sslVersion_),
    sslCipher_(message.sslCipher_),
    protoStr_(message.protoStr_),
    pri_(message.pri_),
    h2Pri_(message.h2Pri_),
    parsedCookies_(message.parsedCookies_),
    parsedQueryParams_(message.parsedQueryParams_),
    chunked_(message.chunked_),
    upgraded_(message.upgraded_),
    wantsKeepalive_(message.wantsKeepalive_),
    trailersAllowed_(message.trailersAllowed_),
    secure_(message.secure_) {
  if (message.trailers_) {
    trailers_.reset(new HTTPHeaders(*message.trailers_.get()));
  }
}

HTTPMessage& HTTPMessage::operator=(const HTTPMessage& message) {
  if (&message == this) {
    return *this;
  }
  startTime_ = message.startTime_;
  seqNo_ = message.seqNo_;
  dstAddress_ = message.dstAddress_;
  dstIP_ = message.dstIP_;
  dstPort_ = message.dstPort_;
  localIP_ = message.localIP_;
  versionStr_ = message.versionStr_;
  fields_ = message.fields_;
  cookies_ = message.cookies_;
  queryParams_ = message.queryParams_;
  version_ = message.version_;
  headers_ = message.headers_;
  strippedPerHopHeaders_ = message.headers_;
  sslVersion_ = message.sslVersion_;
  sslCipher_ = message.sslCipher_;
  protoStr_ = message.protoStr_;
  pri_ = message.pri_;
  h2Pri_ = message.h2Pri_;
  parsedCookies_ = message.parsedCookies_;
  parsedQueryParams_ = message.parsedQueryParams_;
  chunked_ = message.chunked_;
  upgraded_ = message.upgraded_;
  wantsKeepalive_ = message.wantsKeepalive_;
  trailersAllowed_ = message.trailersAllowed_;
  secure_ = message.secure_;

  if (message.trailers_) {
    trailers_.reset(new HTTPHeaders(*message.trailers_.get()));
  } else {
    trailers_.reset();
  }
  return *this;
}

void HTTPMessage::setMethod(HTTPMethod method) {
  Request& req = request();
  req.method_ = method;
}

void HTTPMessage::setMethod(folly::StringPiece method) {
  VLOG(9) << "setMethod: " << method;
  Request& req = request();
  boost::optional<HTTPMethod> result = stringToMethod(method);
  if (result) {
    req.method_ = *result;
  } else {
    req.method_ = method.str();
    auto& storedMethod = boost::get<std::string>(req.method_);
    std::transform(storedMethod.begin(), storedMethod.end(),
                   storedMethod.begin(), ::toupper);
  }
}

boost::optional<HTTPMethod> HTTPMessage::getMethod() const {
  const auto& req = request();
  if (req.method_.which() == 2) {
    return boost::get<HTTPMethod>(req.method_);
  }
  return boost::none;
}

/**
 * @Returns a string representation of the request method (fpreq)
 */
const std::string& HTTPMessage::getMethodString() const {
  const auto& req = request();
  if (req.method_.which() == 1) {
    return boost::get<std::string>(req.method_);
  } else if (req.method_.which() == 2) {
    return methodToString(boost::get<HTTPMethod>(req.method_));
  }
  return empty_string;
}

void HTTPMessage::setHTTPVersion(uint8_t maj, uint8_t min) {
  version_.first = maj;
  version_.second = min;
  versionStr_ = folly::to<string>(maj, ".", min);
}

const pair<uint8_t, uint8_t>& HTTPMessage::getHTTPVersion() const {
  return version_;
}

int HTTPMessage::processMaxForwards() {
  if (getMethod() == HTTPMethod::TRACE || getMethod()  == HTTPMethod::OPTIONS) {
    const string& value = headers_.getSingleOrEmpty(HTTP_HEADER_MAX_FORWARDS);
    if (value.length() > 0) {
      int64_t max_forwards = 0;
      try {
        max_forwards = folly::to<int64_t>(value);
      } catch (const std::range_error& ex) {
        return 400;
      }

      if (max_forwards < 0) {
        return 400;
      } else if (max_forwards == 0) {
        return 501;
      } else {
        headers_.set(HTTP_HEADER_MAX_FORWARDS,
                     folly::to<string>(max_forwards - 1));
      }
    }
  }
  return 0;
}

bool HTTPMessage::isHTTP1_0() const {
  return version_ == kHTTPVersion10;
}

bool HTTPMessage::isHTTP1_1() const {
  return version_ == kHTTPVersion11;
}

namespace {
struct FormattedDate {
  time_t lastTime{0};
  string date;

  string formatDate() {
    const auto now = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now());

    if (now != lastTime) {
      char buff[1024];
      tm timeTupple;
      gmtime_r(&now, &timeTupple);

      strftime(buff, 1024, "%a, %d %b %Y %H:%M:%S %Z", &timeTupple);
      date = std::string(buff);
      lastTime = now;
    }
    return date;
  }
};
}

string HTTPMessage::formatDateHeader() {
  struct DateTag {};
  static folly::SingletonThreadLocal<FormattedDate, DateTag> s_formattedDate{};

  return s_formattedDate.get().formatDate();
}

void HTTPMessage::ensureHostHeader() {
  if (!headers_.exists(HTTP_HEADER_HOST)) {
    headers_.add(HTTP_HEADER_HOST,
                 getDstAddress().getFamily() == AF_INET6
                 ? '[' + getDstIP() + ']' : getDstIP());
  }
}

void HTTPMessage::setStatusCode(uint16_t status) {
  response().status_ = status;
  response().statusStr_ = folly::to<string>(status);
}

uint16_t HTTPMessage::getStatusCode() const {
  return response().status_;
}

void HTTPMessage::setPushStatusCode(uint16_t status) {
  request().pushStatus_ = status;
  request().pushStatusStr_ = folly::to<string>(status);
}

const std::string& HTTPMessage::getPushStatusStr() const{
  return request().pushStatusStr_;
}

uint16_t HTTPMessage::getPushStatusCode() const{
  return request().pushStatus_;
}

void
HTTPMessage::constructDirectResponse(const pair<uint8_t,uint8_t>& version,
                                     const int statusCode,
                                     const string& statusMsg,
                                     int contentLength) {
  setStatusCode(statusCode);
  setStatusMessage(statusMsg);
  constructDirectResponse(version, contentLength);
}

void
HTTPMessage::constructDirectResponse(const pair<uint8_t,uint8_t>& version,
                                     int contentLength) {
  setHTTPVersion(version.first, version.second);

  headers_.set(HTTP_HEADER_CONTENT_LENGTH, folly::to<string>(contentLength));

  if (!headers_.exists(HTTP_HEADER_CONTENT_TYPE)) {
    headers_.add(HTTP_HEADER_CONTENT_TYPE, "text/plain");
  }
  setIsChunked(false);
  setIsUpgraded(false);
}

void HTTPMessage::parseCookies() const {
  DCHECK(!parsedCookies_);
  parsedCookies_ = true;

  headers_.forEachValueOfHeader(HTTP_HEADER_COOKIE,
                                [&](const string& headerval) {
    splitNameValuePieces(headerval, ';', '=',
        [this](StringPiece cookieName, StringPiece cookieValue) {
          cookies_.emplace(cookieName, cookieValue);
        });

    return false; // continue processing "cookie" headers
  });
}

void HTTPMessage::unparseCookies() {
  cookies_.clear();
  parsedCookies_ = false;
}

const StringPiece HTTPMessage::getCookie(const string& name) const {
  // Parse the cookies if we haven't done so yet
  if (!parsedCookies_) {
    parseCookies();
  }

  auto it = cookies_.find(name);
  if (it == cookies_.end()) {
    return StringPiece();
  } else {
    return it->second;
  }
}

void HTTPMessage::parseQueryParams() const {
  DCHECK(!parsedQueryParams_);
  const Request& req = request();

  parsedQueryParams_ = true;
  if (req.query_.empty()) {
    return;
  }

  splitNameValue(req.query_, '&', '=',
        [this] (string&& paramName, string&& paramValue) {

    auto it = queryParams_.find(paramName);
    if (it == queryParams_.end()) {
      queryParams_.emplace(std::move(paramName), std::move(paramValue));
    } else {
      // We have some unit tests that make sure we always return the last
      // value when there are duplicate parameters. I don't think this really
      // matters, but for now we might as well maintain the same behavior.
      it->second = std::move(paramValue);
    }
  });
}

void HTTPMessage::unparseQueryParams() {
  queryParams_.clear();
  parsedQueryParams_ = false;
}

const string* HTTPMessage::getQueryParamPtr(const string& name) const {
  // Parse the query parameters if we haven't done so yet
  if (!parsedQueryParams_) {
    parseQueryParams();
  }

  auto it = queryParams_.find(name);
  if (it == queryParams_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool HTTPMessage::hasQueryParam(const string& name) const {
  return getQueryParamPtr(name) != nullptr;
}

const string& HTTPMessage::getQueryParam(const string& name) const {
  const string* ret = getQueryParamPtr(name);
  return ret ? *ret : empty_string;
}

int HTTPMessage::getIntQueryParam(const std::string& name) const {
  return folly::to<int>(getQueryParam(name));
}

int HTTPMessage::getIntQueryParam(const std::string& name, int defval) const {
  try {
    return getIntQueryParam(name);
  } catch (const std::exception& ex) {
  }

  return defval;
}

std::string HTTPMessage::getDecodedQueryParam(const std::string& name) const {
  auto val = getQueryParam(name);

  std::string result;
  try {
    folly::uriUnescape(val, result, folly::UriEscapeMode::QUERY);
  } catch (const std::exception& ex) {
    LOG(WARNING) << "Invalid escaped query param: " << folly::exceptionStr(ex);
  }
  return result;
}

const std::map<std::string, std::string>& HTTPMessage::getQueryParams() const {
  // Parse the query parameters if we haven't done so yet
  if (!parsedQueryParams_) {
    parseQueryParams();
  }
  return queryParams_;
}

bool HTTPMessage::setQueryString(const std::string& query) {
  ParseURL u(request().url_);

  if (u.valid()) {
    // Recreate the URL by just changing the query string
    request().url_ = createUrl(u.scheme(),
                               u.authority(),
                               u.path(),
                               query, // new query string
                               u.fragment());
    request().query_ = query;
    return true;
  }

  VLOG(4) << "Error parsing URL during setQueryString: " << request().url_;
  return false;
}

bool HTTPMessage::removeQueryParam(const std::string& name) {
  // Parse the query parameters if we haven't done so yet
  if (!parsedQueryParams_) {
    parseQueryParams();
  }

  if (!queryParams_.erase(name)) {
    // Query param was not found.
    return false;
  }

  auto query = createQueryString(queryParams_, request().query_.length());
  return setQueryString(query);
}

bool HTTPMessage::setQueryParam(const std::string& name,
    const std::string& value) {
  // Parse the query parameters if we haven't done so yet
  if (!parsedQueryParams_) {
    parseQueryParams();
  }

  queryParams_[name] = value;
  auto query = createQueryString(queryParams_, request().query_.length());
  return setQueryString(query);
}

std::string HTTPMessage::createQueryString(
    const std::map<std::string, std::string>& params, uint32_t maxLength) {
  std::string query;
  query.reserve(maxLength);
  for (auto it = params.begin(); it != params.end(); it++) {
    if (it != params.begin()) {
      query.append("&");
    }
    query.append(it->first + "=" + it->second);
  }
  query.shrink_to_fit();
  return query;
}

std::string HTTPMessage::createUrl(const folly::StringPiece scheme,
                                   const folly::StringPiece authority,
                                   const folly::StringPiece path,
                                   const folly::StringPiece query,
                                   const folly::StringPiece fragment) {
  std::string url;
  url.reserve(scheme.size() + authority.size() + path.size() + query.size() +
                 fragment.size() + 5); // 5 chars for ://,? and #
  if (!scheme.empty()) {
    folly::toAppend(scheme.str(), "://", &url);
  }
  folly::toAppend(authority, path, &url);
  if (!query.empty()) {
    folly::toAppend('?', query, &url);
  }
  if (!fragment.empty()) {
    folly::toAppend('#', fragment, &url);
  }
  url.shrink_to_fit();
  return url;
}

void HTTPMessage::splitNameValuePieces(
        const string& input,
        char pairDelim,
        char valueDelim,
        std::function<void(StringPiece, StringPiece)> callback) {

  StringPiece sp(input);
  while (!sp.empty()) {
    size_t pairDelimPos = sp.find(pairDelim);
    StringPiece keyValue;

    if (pairDelimPos == string::npos) {
      keyValue = sp;
      sp.advance(sp.size());
    } else {
      keyValue = sp.subpiece(0, pairDelimPos);
      // Skip '&' char
      sp.advance(pairDelimPos + 1);
    }

    if (keyValue.empty()) {
      continue;
    }

    size_t valueDelimPos = keyValue.find(valueDelim);
    if (valueDelimPos == string::npos) {
      // Key only query param
      callback(trim(keyValue), StringPiece());
    } else {
      auto name = keyValue.subpiece(0, valueDelimPos);
      auto value = keyValue.subpiece(valueDelimPos + 1);
      callback(trim(name), trim(value));
    }
  }
}

StringPiece HTTPMessage::trim(StringPiece sp) {
  // TODO: use a library function from boost?
  for (; !sp.empty() && sp.front() == ' '; sp.pop_front()) {
  }
  for (; !sp.empty() && sp.back() == ' '; sp.pop_back()) {
  }
  return sp;
}

void HTTPMessage::splitNameValue(
        const string& input,
        char pairDelim,
        char valueDelim,
        std::function<void(string&&, string&&)> callback) {

  folly::StringPiece sp(input);
  while (!sp.empty()) {
    size_t pairDelimPos = sp.find(pairDelim);
    folly::StringPiece keyValue;

    if (pairDelimPos == string::npos) {
      keyValue = sp;
      sp.advance(sp.size());
    } else {
      keyValue = sp.subpiece(0, pairDelimPos);
      // Skip '&' char
      sp.advance(pairDelimPos + 1);
    }

    if (keyValue.empty()) {
      continue;
    }

    size_t valueDelimPos = keyValue.find(valueDelim);
    if (valueDelimPos == string::npos) {
      // Key only query param
      string name = keyValue.str();
      string value;

      boost::trim(name, defaultLocale);
      callback(std::move(name), std::move(value));
    } else {
      string name = keyValue.subpiece(0, valueDelimPos).str();
      string value = keyValue.subpiece(valueDelimPos + 1).str();

      boost::trim(name, defaultLocale);
      boost::trim(value, defaultLocale);
      callback(std::move(name), std::move(value));
    }
  }
}

void HTTPMessage::dumpMessage(int vlogLevel) const {
  VLOG(vlogLevel) << ", chunked: " << chunked_
                  << ", upgraded: " << upgraded_
                  << ", Fields for message:";

  // Common fields to both requests and responses.
  std::vector<std::pair<const char*, const std::string*>> fields {{
    {"local_ip", &localIP_},
    {"version", &versionStr_},
    {"dst_ip", &dstIP_},
    {"dst_port", &dstPort_},
  }};

  if (fields_.type() == typeid(Request)) {
    // Request fields.
    const Request& req = request();
    fields.push_back(make_pair("client_ip", &req.clientIP_));
    fields.push_back(make_pair("client_port", &req.clientPort_));
    fields.push_back(make_pair("method", &getMethodString()));
    fields.push_back(make_pair("path", &req.path_));
    fields.push_back(make_pair("query", &req.query_));
    fields.push_back(make_pair("url", &req.url_));
    fields.push_back(make_pair("push_status", &req.pushStatusStr_));
  } else if (fields_.type() == typeid(Response)) {
    // Response fields.
    const Response& resp = response();
    fields.push_back(make_pair("status", &resp.statusStr_));
    fields.push_back(make_pair("status_msg", &resp.statusMsg_));
  }

  for (auto field : fields) {
    if (!field.second->empty()) {
      VLOG(vlogLevel) << " " << field.first
                      << ":" << stripCntrlChars(*field.second);
    }
  }

  headers_.forEach([&] (const string& h, const string& v) {
    VLOG(vlogLevel) << " " << stripCntrlChars(h) << ": "
                    << stripCntrlChars(v);
  });
}

void
HTTPMessage::atomicDumpMessage(int vlogLevel) const {
  std::lock_guard<std::mutex> g(mutexDump_);
  dumpMessage(vlogLevel);
}

void HTTPMessage::dumpMessageToSink(google::LogSink* logSink) const {
  LOG_TO_SINK(logSink, INFO) << "Version: " << versionStr_
                  << ", chunked: " << chunked_
                  << ", upgraded: " << upgraded_;

  // Common fields to both requests and responses.
  std::vector<std::pair<const char*, const std::string*>> fields {{
    {"local_ip", &localIP_},
    {"version", &versionStr_},
    {"dst_ip", &dstIP_},
    {"dst_port", &dstPort_},
  }};

  if (fields_.type() == typeid(Request)) {
    // Request fields.
    const Request& req = request();
    fields.push_back(make_pair("client_ip", &req.clientIP_));
    fields.push_back(make_pair("client_port", &req.clientPort_));
    fields.push_back(make_pair("method", &getMethodString()));
    fields.push_back(make_pair("path", &req.path_));
    fields.push_back(make_pair("query", &req.query_));
    fields.push_back(make_pair("url", &req.url_));
    fields.push_back(make_pair("push_status", &req.pushStatusStr_));
  } else if (fields_.type() == typeid(Response)) {
    // Response fields.
    const Response& resp = response();
    fields.push_back(make_pair("status", &resp.statusStr_));
    fields.push_back(make_pair("status_msg", &resp.statusMsg_));
  }

  LOG_TO_SINK(logSink, INFO) << "Fields for message: ";
  for (auto field : fields) {
    if (!field.second->empty()) {
      LOG_TO_SINK(logSink, INFO) << " " << field.first
                                 << ":" << folly::backslashify(*field.second);
    }
  }

  LOG_TO_SINK(logSink, INFO) << "Headers for message: ";
  headers_.forEach([&logSink] (const string& h, const string& v) {
    LOG_TO_SINK(logSink, INFO) << " " << folly::backslashify(h)
                               << ": " << folly::backslashify(v);
  });
}

bool HTTPMessage::computeKeepalive() const {
  if (version_.first < 1) {
    return false;
  }

  // RFC 2616 isn't explicitly clear about whether "close" is case-sensitive.
  // Section 2.1 states that literal tokens in the BNF are case-insensitive
  // unless stated otherwise.  The "close" token isn't explicitly mentioned
  // in the BNF, but other header fields such as the character set and
  // content coding are explicitly called out as being case insensitive.
  //
  // We'll treat the "close" token case-insensitively.  This is the most
  // conservative approach, since disabling keepalive when it was requested
  // is better than enabling keepalive for a client that didn't expect it.
  //
  // Note that we only perform ASCII lowering here.  This is good enough,
  // since the token we are looking for is ASCII.
  if (checkForHeaderToken(HTTP_HEADER_CONNECTION, "close", false)) {
    // The Connection header contained a "close" token, so keepalive
    // is disabled.
    return false;
  }

  if (version_ == kHTTPVersion10) {
      // HTTP 1.0 persistent connections require a Connection: Keep-Alive
      // header to be present for the connection to be persistent.
      if (checkForHeaderToken(HTTP_HEADER_CONNECTION, "keep-alive", false)) {
        return true;
      }
      return false;
  }

  // It's a keepalive connection.
  return true;
}

bool HTTPMessage::checkForHeaderToken(const HTTPHeaderCode headerCode,
                                      char const* token,
                                      bool caseSensitive) const {
  StringPiece tokenPiece(token);
  string lowerToken;
  if (!caseSensitive) {
    lowerToken = token;
    boost::to_lower(lowerToken, defaultLocale);
    tokenPiece.reset(lowerToken);
  }
  // Search through all of the headers with this name.
  // forEachValueOfHeader will return true iff it was "broken" prematurely
  // with "return true" in the lambda-function
  return headers_.forEachValueOfHeader(headerCode, [&] (const string& value) {
    string lower;
    // Use StringPiece, since it implements a faster find() than std::string
    StringPiece headerValue;
    if (caseSensitive) {
      headerValue.reset(value);
    } else {
      // TODO: We only perform ASCII lowering right now.  Technically the
      // headers could contain data in other encodings, if encoded according
      // to RFC 2047 (encoded strings will start with "=?").
      lower = value;
      boost::to_lower(lower, defaultLocale);
      headerValue.reset(lower);
    }

    // Look for the specified token
    size_t idx = 0;
    size_t end = headerValue.size();
    while (idx < end) {
      idx = headerValue.find(tokenPiece, idx);
      if (idx == string::npos) {
        break;
      }

      // Search backwards to make sure we found the value at the beginning
      // of a token.
      bool at_token_start = false;
      size_t prev = idx;
      while (true) {
        if (prev == 0) {
          at_token_start = true;
          break;
        }
        --prev;
        char c = headerValue[prev];
        if (c == ',') {
          at_token_start = true;
          break;
        }
        if (!isLWS(c)) {
          // not at a token start
          break;
        }
      }
      if (!at_token_start) {
        idx += 1;
        continue;
      }

      // Search forwards to see if we found the value at the end of a token
      bool at_token_end = false;
      size_t next = idx + tokenPiece.size();
      while (true) {
        if (next >= end) {
          at_token_end = true;
          break;
        }
        char c = headerValue[next];
        if (c == ',') {
          at_token_end = true;
          break;
        }
        if (!isLWS(c)) {
          // not at a token end
          break;
        }
        ++next;
      }
      if (at_token_end) {
        // We found the token we're looking for
        return true;
      }

      idx += 1;
    }
    return false; // keep processing
  });
}

const char* HTTPMessage::getDefaultReason(uint16_t status) {
  switch (status) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
  }

  // Note: Some Microsoft clients behave badly if the reason string
  // is left empty.  Therefore return a non-empty string here.
  return "-";
}

} // proxygen
