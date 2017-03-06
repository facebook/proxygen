#pragma once

#include <proxygen/lib/http/HTTPCommonHeaders.h>

#include <bitset>

namespace proxygen {

/**
 * HTTPHeaderSet is a convenient way to maintain a bitset of headers.
 * It's a std::bitset with a constructor that allows an initializer list
 * of header codes to be specified (as true bits).
 */
class HTTPHeaderSet: public std::bitset<256> {
 public:
  HTTPHeaderSet() = default;
  HTTPHeaderSet(const HTTPHeaderSet&) = default;
  HTTPHeaderSet(HTTPHeaderSet&&) = default;
  HTTPHeaderSet(std::initializer_list<HTTPHeaderCode> lc) :
    bitset<256>() {
    for (const auto& code: lc)
      set(code);
  }
};

}
