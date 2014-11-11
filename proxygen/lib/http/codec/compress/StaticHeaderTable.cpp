/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/StaticHeaderTable.h>

#include <glog/logging.h>
#include <list>

using std::list;

namespace proxygen {

namespace {

// instantiate the static table
StaticHeaderTable table;

}

StaticHeaderTable::StaticHeaderTable() : HeaderTable() {
  static const char* STATIC_TABLE[][2] {
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "500"},
    {":status", "404"},
    {":status", "403"},
    {":status", "400"},
    {":status", "401"},
    {"accept-charset", ""},
    {"accept-encoding", ""},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""}
  };

  // calculate the size
  list<HPACKHeader> hlist;
  uint32_t byteCount = 0;
  for (size_t i = 0; i < sizeof(STATIC_TABLE) / sizeof(STATIC_TABLE[0]); i++) {
    hlist.push_back(HPACKHeader(STATIC_TABLE[i][0], STATIC_TABLE[i][1]));
    byteCount += hlist.back().bytes();
  }
  // initialize with a capacity that will exactly fit the static headers
  init(byteCount);
  hlist.reverse();
  for (auto& header : hlist) {
    add(header);
  }
  // the static table is not involved in the delta compression
  clearReferenceSet();
}

const HeaderTable& StaticHeaderTable::get() {
  return table;
}

}
