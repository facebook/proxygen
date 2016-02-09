/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/StaticHeaderTable.h>
#include <proxygen/lib/utils/UnionBasedStatic.h>

#include <glog/logging.h>
#include <list>

using std::list;
using std::string;
using std::vector;

namespace proxygen {

// array of static header table entires pair
const char* s_tableEntries[][2] = {
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

const int kEntriesSize = sizeof(s_tableEntries) / (2 * sizeof(const char*));

/**
 * we're using a union to prevent the static object destruction when
 * calling exit() from a different thread, common on mobile
 */
DEFINE_UNION_STATIC_CONST_NO_INIT(StaticHeaderTable, StaticTable, s_table);

static struct InitStaticTable {
public:
  InitStaticTable() {
    // use placement new to initialize the static table
    new (const_cast<StaticHeaderTable*>(&s_table.data))
      StaticHeaderTable(s_tableEntries, kEntriesSize);
  }
} s_InitStaticTable;

StaticHeaderTable::StaticHeaderTable(
    const char* entries[][2],
    int size)
    : HeaderTable() {
  // calculate the size
  list<HPACKHeader> hlist;
  uint32_t byteCount = 0;
  for (int i = 0; i < size; ++i) {
    hlist.push_back(HPACKHeader(entries[i][0], entries[i][1]));
    byteCount += hlist.back().bytes();
  }
  // initialize with a capacity that will exactly fit the static headers
  init(byteCount);
  hlist.reverse();
  for (const auto& header : hlist) {
    add(header);
  }
  // the static table is not involved in the delta compression
  clearReferenceSet();
}

const HeaderTable& StaticHeaderTable::get() {
  return s_table.data;
}

}
