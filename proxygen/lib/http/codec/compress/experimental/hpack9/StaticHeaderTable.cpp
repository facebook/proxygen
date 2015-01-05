/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/hpack9/StaticHeaderTable.h>

#include <proxygen/lib/utils/UnionBasedStatic.h>

namespace proxygen { namespace HPACK09 {

DEFINE_UNION_STATIC_CONST_NO_INIT(StaticHeaderTable, StaticTable, s_table);

__attribute__((__constructor__))
void initStaticTable() {
  new (const_cast<StaticHeaderTable*>(&s_table.data)) StaticHeaderTable ({
      {":authority", ""},
      {":method", "GET"},
      {":method", "POST"},
      {":path", "/"},
      {":path", "/index.html"},
      {":scheme", "http"},
      {":scheme", "https"},
      {":status", "200"},
      {":status", "204"},
      {":status", "206"},
      {":status", "304"},
      {":status", "400"},
      {":status", "404"},
      {":status", "500"},
      {"accept-charset", ""},
      {"accept-encoding", "gzip, deflate"},
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
    });
}

const HeaderTable& getStaticTable() {
  return s_table.data;
}

}}
