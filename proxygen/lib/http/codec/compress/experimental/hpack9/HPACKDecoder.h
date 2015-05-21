/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <proxygen/lib/http/codec/compress/HPACKDecoder.h>

#include <proxygen/lib/http/codec/compress/experimental/hpack9/StaticHeaderTable.h>
#include <proxygen/lib/http/codec/compress/experimental/hpack9/HPACKContextImpl.h>

namespace proxygen {

class HPACKDecoder09 : public HPACKDecoder {
public:
  explicit HPACKDecoder09(
    uint32_t tableSize=HPACK::kTableSize,
    uint32_t maxUncompressed=HeaderCodec::kMaxUncompressed)
      : HPACKDecoder(HPACK::MessageType::RESP,
                     tableSize,
                     maxUncompressed,
                     Version::HPACK09) {}

  void handleTableSizeUpdate(HPACKDecodeBuffer& dbuf);

 protected:
  const HeaderTable& getStaticTable() const override {
    return HPACK09::getStaticTable();
  }

  bool isStatic(uint32_t index) const override {
    return HPACKContextImpl::isStatic(index, getStaticTable().size());
  }

  uint32_t globalToDynamicIndex(uint32_t index) const override {
    return HPACKContextImpl::globalToDynamicIndex(index,
                                                  getStaticTable().size());
  }
  uint32_t globalToStaticIndex(uint32_t index) const override {
    return HPACKContextImpl::globalToStaticIndex(index);
  }
  uint32_t dynamicToGlobalIndex(uint32_t index) const override {
    return HPACKContextImpl::dynamicToGlobalIndex(index,
                                                  getStaticTable().size());
  }
  uint32_t staticToGlobalIndex(uint32_t index) const override {
    return HPACKContextImpl::staticToGlobalIndex(index);
  }

  uint32_t getIndex(const HPACKHeader& header) const override {
    return HPACKContextImpl::getIndex(header, getStaticTable(), table_);
  }

  uint32_t nameIndex(const std::string& name) const override {
    return HPACKContextImpl::nameIndex(name, getStaticTable(), table_);
  }

  uint32_t emitRefset(headers_t& emitted) override { return 0; }

  const huffman::HuffTree& getHuffmanTree() const override;

  uint32_t decodeLiteralHeader(HPACKDecodeBuffer& dbuf,
                               headers_t* emitted) override;
  uint32_t decodeIndexedHeader(HPACKDecodeBuffer& dbuf,
                               headers_t* emitted) override;
};

}
