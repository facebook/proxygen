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

#include <folly/FBVector.h>
#include <folly/Range.h>
#include <proxygen/lib/http/HTTPCommonHeaders.h>
#include <proxygen/lib/utils/Export.h>
#include <proxygen/lib/utils/UtilInl.h>

#include <bitset>
#include <cstring>
#include <string>

namespace proxygen {

extern const std::string empty_string;

/**
 * Return true if the character is linear whitespace, as defined by the LWS
 * definition in RFC 2616, and false otherwise
 */
inline bool isLWS(char c) {
  // Technically \r and \n are only allowed in LWS if they appear together.
  if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
    return true;
  }
  return false;
}

/**
 * A collection of HTTP headers.
 *
 * This is broken out from HTTPMessage, as it's convenient for other things to
 * be able to use collections of HTTP headers that are easy to work with. The
 * structure is optimized for real-life header collection sizes.
 *
 * Headers are stored as Name/Value pairs, in the order they are received on
 * the wire. We hash the names of all common HTTP headers (using a static
 * perfect hash function generated using gperf from HTTPCommonHeaders.gperf)
 * into 1-byte hashes (we call them "codes") and only store these. We search
 * them using memchr, which has an x86_64 assembly implementation with
 * complexity O(n/16) ;)
 *
 * Instead of creating strings with header names, we point to a static array
 * of strings in HTTPCommonHeaders. If the header name is not in our set of
 * common header names (this is considered unlikely, because we intend this set
 * to be very complete), then we create a new string with its name (we own that
 * pointer then). For such headers, we store the code HTTP_HEADER_OTHER.
 *
 * The code HTTP_HEADER_NONE signifies a header that has been removed.
 *
 * Most methods which take a header name have two versions: one accepting
 * a string, and one accepting a code. It is recommended to use the latter
 * if possible, as in:
 *     headers.add(HTTP_HEADER_LOCATION, location);
 * rather than:
 *     headers.add("Location", location);
 */
class HTTPHeaders {
 public:
  /*
   * separator used to concatenate multiple values of the same header
   * check out sections 4.2 and 14.45 from rfc2616
   */
  static const std::string COMBINE_SEPARATOR;

  FB_EXPORT HTTPHeaders();
  FB_EXPORT ~HTTPHeaders();
  FB_EXPORT HTTPHeaders (const HTTPHeaders&);
  FB_EXPORT HTTPHeaders& operator= (const HTTPHeaders&);
  FB_EXPORT HTTPHeaders (HTTPHeaders&&) noexcept;
  FB_EXPORT HTTPHeaders& operator= (HTTPHeaders&&);

  /**
   * Add the header 'name' with value 'value'; if other instances of this
   * header name exist, they will be retained.
   */
  void add(folly::StringPiece name, folly::StringPiece value);
  template <typename T> // T = string
  void add(folly::StringPiece name, T&& value);
  template <typename T> // T = string
  void add(HTTPHeaderCode code, T&& value);
  void rawAdd(const std::string& name, const std::string& value);

  void addFromCodec(const char* str, size_t len, std::string&& value);

  /**
   * For the header 'name', set its value to the single header 'value',
   * removing any other instances of this header.
   */
  void set(folly::StringPiece name, const std::string& value) {
    // this could be somewhat optimized but probably not an issue yet
    remove(name);
    add(name, value);
  }
  void set(HTTPHeaderCode code, const std::string& value) {
    remove(code);
    add(code, value);
  }
  void rawSet(const std::string& name, const std::string& value) {
    set(name, value);
  }

  /**
   * Do we have an instance of the given header?
   */
  bool exists(folly::StringPiece name) const;
  bool exists(HTTPHeaderCode code) const;
  bool rawExists(std::string& name) const {
    return exists(name);
  }

  /**
   * combine all the value for this header into a string
   */
  template <typename T>
  std::string combine(const T& header,
                      const std::string& separator=COMBINE_SEPARATOR) const;

  /**
   * Process the list of all headers, in the order that they were seen:
   * for each header:value pair, the function/functor/lambda-expression
   * given as the second parameter will be executed. It should take two
   * const string & parameters and return void. Example use:
   *     hdrs.forEach([&] (const string& header, const string& val) {
   *       std::cout << header << ": " << val;
   *     });
   */
  template <typename LAMBDA> // (const string &, const string &) -> void
  inline void forEach(LAMBDA func) const;

  /**
   * Process the list of all headers, in the order that they were seen:
   * for each header:value pair, the function/functor/lambda-expression
   * given as the second parameter will be executed. It should take one
   * HTTPHeaderCode (code) parameter, two const string & parameters and
   * return void. Example use:
   *     hdrs.forEachWithCode([&] (HTTPHeaderCode code,
   *                               const string& header,
   *                               const string& val) {
   *       std::cout << header << "(" << code << "): " << val;
   *     });
   */
  template <typename LAMBDA>
  inline void forEachWithCode(LAMBDA func) const;

  /**
   * Process the list of all headers, in the order that they were seen:
   * for each header:value pair, the function/functor/lambda-expression
   * given as the parameter will be executed to determine whether the
   * header should be removed. Example use:
   *
   *     hdrs.removeByPredicate([&] (HTTPHeaderCode code,
   *                                 const string& header,
   *                                 const string& val) {
   *       return boost::regex_match(header, "^X-Fb-.*");
   *     });
   *
   * return true only if one or more headers are removed.
   */
  template <typename LAMBDA> // (const string &, const string &) -> bool
  inline bool removeByPredicate(LAMBDA func);

  /**
   * Returns the value of the header if it's found in the message and is the
   * only value under the given name. If either of these is violated, returns
   * empty_string.
   */
  template <typename T> // either uint8_t or string
  const std::string & getSingleOrEmpty(const T& nameOrCode) const;
  const std::string rawGet(const std::string& header) const {
    return getSingleOrEmpty(header);
  }

  /**
   * Get the number of values corresponding to a given header name.
   */
  size_t getNumberOfValues(HTTPHeaderCode code) const;
  size_t getNumberOfValues(folly::StringPiece name) const;

  /**
   * Process the ordered list of values for the given header name:
   * for each value, the function/functor/lambda-expression given as the second
   * parameter will be executed. It should take one const string & parameter
   * and return bool (false to keep processing, true to stop it). Example use:
   *     hdrs.forEachValueOfHeader("someheader", [&] (const string& val) {
   *       std::cout << val;
   *       return false;
   *     });
   * This method returns true if processing was stopped (by func returning
   * true), and false otherwise.
   */
  template <typename LAMBDA> // const string & -> bool
  inline bool forEachValueOfHeader(folly::StringPiece name, LAMBDA func) const;
  template <typename LAMBDA> // const string & -> bool
  inline bool forEachValueOfHeader(HTTPHeaderCode code, LAMBDA func) const;

  /**
   * Remove all instances of the given header, returning true if anything was
   * removed and false if this header didn't exist in our set.
   */
  bool remove(folly::StringPiece name);
  bool remove(HTTPHeaderCode code);
  void rawRemove(const std::string& name) {
    remove(name);
  }

  /**
   * Remove all headers.
   */
  void removeAll();

  /**
   * Remove per-hop-headers and headers named in the Connection header
   * and place the value in strippedHeaders
   */
  void stripPerHopHeaders(HTTPHeaders& strippedHeaders);

  /**
   * Get the total number of headers.
   */
  size_t size() const;

  /**
   * Copy all headers from this to hdrs.
   */
  void copyTo(HTTPHeaders& hdrs) const;

  /**
   * Determines whether header with a given code is a per-hop header,
   * which should be stripped by stripPerHopHeaders().
   */
  static std::bitset<256>& perHopHeaderCodes();

 private:
  // vector storing the 1-byte hashes of header names
  folly::fbvector<HTTPHeaderCode> codes_;

  /**
   * Vector storing pointers to header names; we own those pointers which
   * correspond to HTTP_HEADER_OTHER codes.
   */
  folly::fbvector<const std::string *> headerNames_;

  folly::fbvector<std::string> headerValues_;

  size_t deletedCount_;

  /**
   * The initial capacity of the three vectors, reserved right after
   * construction.
   */
  static const size_t kInitialVectorReserve = 16;

  /**
   * Moves the named header and values from this group to the destination
   * group.  No-op if the header doesn't exist.  Returns true if header(s) were
   * moved.
   */
  bool transferHeaderIfPresent(folly::StringPiece name, HTTPHeaders& dest);

  // deletes the strings in headerNames_ that we own
  void disposeOfHeaderNames();
};

// Implementation follows - it has to be in the .h because of the templates

template <typename T> // T = string
void HTTPHeaders::add(folly::StringPiece name, T&& value) {
  assert(name.size());
  const HTTPHeaderCode code = HTTPCommonHeaders::hash(name.data(), name.size());
  codes_.push_back(code);
  headerNames_.push_back((code == HTTP_HEADER_OTHER)
      ? new std::string(name.data(), name.size())
      : HTTPCommonHeaders::getPointerToHeaderName(code));
  headerValues_.emplace_back(std::forward<T>(value));
}

template <typename T> // T = string
void HTTPHeaders::add(HTTPHeaderCode code, T&& value) {
  codes_.push_back(code);
  headerNames_.push_back(HTTPCommonHeaders::getPointerToHeaderName(code));
  headerValues_.emplace_back(std::forward<T>(value));
}

// iterate over the positions (in vector) of all headers with given code
#define ITERATE_OVER_CODES(Code, Block) { \
  const HTTPHeaderCode* ptr = codes_.data(); \
  while(true) { \
    ptr = (HTTPHeaderCode*) memchr((void*)ptr, (Code), \
                            codes_.size() - (ptr - codes_.data())); \
    if (ptr == nullptr) break; \
    const size_t pos = ptr - codes_.data(); \
    {Block} \
    ptr++; \
  } \
}

// iterate over the positions of all headers with given name
#define ITERATE_OVER_STRINGS(String, Block) \
    ITERATE_OVER_CODES(HTTP_HEADER_OTHER, { \
  if (caseInsensitiveEqual((String), *headerNames_[pos])) { \
    {Block} \
  } \
})

template <typename LAMBDA> // (const string &, const string &) -> void
void HTTPHeaders::forEach(LAMBDA func) const {
  for (size_t i = 0; i < codes_.size(); ++i) {
    if (codes_[i] != HTTP_HEADER_NONE) {
      func(*headerNames_[i], headerValues_[i]);
    }
  }
}

template <typename LAMBDA>
void HTTPHeaders::forEachWithCode(LAMBDA func) const {
  for (size_t i = 0; i < codes_.size(); ++i) {
    if (codes_[i] != HTTP_HEADER_NONE) {
      func(codes_[i], *headerNames_[i], headerValues_[i]);
    }
  }
}

template <typename LAMBDA> // const string & -> bool
bool HTTPHeaders::forEachValueOfHeader(folly::StringPiece name,
                                       LAMBDA func) const {
  const HTTPHeaderCode code = HTTPCommonHeaders::hash(name.data(), name.size());
  if (code != HTTP_HEADER_OTHER) {
    return forEachValueOfHeader(code, func);
  } else {
    ITERATE_OVER_STRINGS(name, {
      if (func(headerValues_[pos])) {
        return true;
      }
    });
    return false;
  }
}

template <typename LAMBDA> // const string & -> bool
bool HTTPHeaders::forEachValueOfHeader(HTTPHeaderCode code,
                                       LAMBDA func) const {
  ITERATE_OVER_CODES(code, {
    if (func(headerValues_[pos])) {
      return true;
    }
  });
  return false;
}

template <typename T>
std::string HTTPHeaders::combine(const T& header,
                                 const std::string& separator) const {
  std::string combined = "";
  forEachValueOfHeader(header, [&] (const std::string& value) -> bool {
      if (combined.empty()) {
        combined.append(value);
      } else {
        combined.append(separator).append(value);
      }
      return false;
    });
  return combined;
}

// LAMBDA: (HTTPHeaderCode, const string&, const string&) -> bool
template <typename LAMBDA>
bool HTTPHeaders::removeByPredicate(LAMBDA func) {
  bool removed = false;
  for (size_t i = 0; i < codes_.size(); ++i) {
    if (codes_[i] == HTTP_HEADER_NONE ||
        !func(codes_[i], *headerNames_[i], headerValues_[i])) {
      continue;
    }

    if (codes_[i] == HTTP_HEADER_OTHER) {
      delete headerNames_[i];
      headerNames_[i] = nullptr;
    }

    codes_[i] = HTTP_HEADER_NONE;
    ++deletedCount_;
    removed = true;
  }

  return removed;
}

template <typename T> // either uint8_t or string
const std::string & HTTPHeaders::getSingleOrEmpty(const T& nameOrCode) const {
  const std::string* res = nullptr;
  forEachValueOfHeader(nameOrCode, [&] (const std::string& value) -> bool {
    if (res != nullptr) {
      // a second value is found
      res = nullptr;
      return true; // stop processing
    } else {
      // the first value is found
      res = &value;
      return false;
    }
  });
  if (res == nullptr) {
    return empty_string;
  } else {
    return *res;
  }
}

#ifndef PROXYGEN_HTTPHEADERS_IMPL
#undef ITERATE_OVER_CODES
#undef ITERATE_OVER_STRINGS
#endif // PROXYGEN_HTTPHEADERS_IMPL

}
