#include <cinttypes>

#include <folly/String.h>

#include "AutoETag.h"

using std::string;

namespace proxygen {

AutoETag::AutoETag(RequestHandler* upstream) : Filter(upstream) {}

void AutoETag::onRequest(std::unique_ptr<HTTPMessage> headers) noexcept {
  const auto& h = headers->getHeaders();

  if_none_match_.reserve(h.getNumberOfValues(HTTP_HEADER_IF_NONE_MATCH));

  h.forEachValueOfHeader(HTTP_HEADER_IF_NONE_MATCH,
      [&if_none_match_ = this->if_none_match_](const string& value) -> bool {
        if (value.find(',') == string::npos) {
          // single entity-tag.
          if_none_match_.push_back(value);
        } else {
          // potentially multiple entity-tags.
          folly::StringPiece vals(value);
          while (!vals.empty()) {
            // skip LWS
            while (!vals.empty() && isLWS(vals[0]))
              vals.pop_front();

            if (vals.empty())
              break;

            if (vals[0] == ',') {
              // null token
              vals.pop_front();
              continue;
            }

            // quoted-string starts. Look for close quote being sure to skip
            // over any quoted-pair (eg. `\"`).
            folly::StringPiece::size_type end;
            for (end = vals.find_first_of("\"\\", 1);
                 end != folly::StringPiece::npos && vals[end] != '"';
                 end = vals.find_first_of("\"\\", end + 1)) {
              // end points to a quoted-pair, skip it and keep searching.
              ++end;
            }

            if (end != folly::StringPiece::npos && vals[end] == '"')
              if_none_match_.emplace_back(vals[0], vals[end]);
            else
              break;

            vals.advance(end + 1);
          }
        }
        return false;
      });

  upstream_->onRequest(std::move(headers));
}

void AutoETag::sendHeaders(HTTPMessage& msg) noexcept {
  HTTPHeaders& headers = msg.getHeaders();

  if (headers.exists(HTTP_HEADER_ETAG)) {
    const string& etag = headers.getSingleOrEmpty(HTTP_HEADER_ETAG);

    if (etagMatches(etag, if_none_match_)) {
      send304NotModified(etag);
      return;
    }
  }

  if (msg.getIsChunked()) {
    // Patch this handler out of the chain.
    // In future we could send the ETag in a trailer instead.
    upstream_->setResponseHandler(downstream_);
    downstream_->sendHeaders(msg);
    return;
  }

  msg_ = msg;

  hasher_.Init(0, 0);
}

void AutoETag::sendBody(std::unique_ptr<folly::IOBuf> body) noexcept {
  hasher_.Update(body->data(), body->length());

  if (body_)
    body_->appendChain(std::move(body));
  else
    body_ = std::move(body);
}

void AutoETag::sendEOM() noexcept {
  uint64_t hash1, hash2;
  hasher_.Final(&hash1, &hash2);

  // ETags are always quoted-strings
  string etag = folly::stringPrintf("\"%016" PRIx64, hash1);
  folly::stringAppendf(&etag, "%016" PRIx64 "\"", hash2);

  if (etagMatches(etag, if_none_match_)) {
    send304NotModified(etag);
    return;
  }

  HTTPHeaders& headers = msg_.getHeaders();
  headers.set(HTTP_HEADER_ETAG, etag);
  downstream_->sendHeaders(msg_);
  downstream_->sendBody(std::move(body_));
  downstream_->sendEOM();
}

bool AutoETag::etagMatches(const string& etag, const std::vector<string>& etags) noexcept {
  for (const auto& tag: etags) {
    if (tag == etag || tag == "*")
      return true;
  }

  return false;
}

void AutoETag::send304NotModified(const string& etag) noexcept {
  msg_.setStatusCode(304);
  msg_.setStatusMessage("Not Modified");

  HTTPHeaders& headers = msg_.getHeaders();
  headers.remove(HTTP_HEADER_CONTENT_LENGTH);
  if (!etag.empty())
    headers.set(HTTP_HEADER_ETAG, etag);

  downstream_->sendHeaders(msg_);
  downstream_->sendEOM();
}

RequestHandler* AutoETagFilterFactory::onRequest(RequestHandler* h, HTTPMessage* msg) noexcept {
  return new AutoETag(h);
}

}
