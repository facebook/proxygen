/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKDecoder.h>
#include <proxygen/lib/http/codec/compress/experimental/qpack/QPACKConstants.h>
// for hpack2headerCodecError
#include <proxygen/lib/http/codec/compress/HPACKDecoder.h>

#include <algorithm>
#include <folly/Memory.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <proxygen/lib/http/codec/compress/Huffman.h>

using folly::IOBuf;
using folly::io::Cursor;
using std::list;
using std::string;
using std::unique_ptr;
using std::vector;
using proxygen::HPACK::DecodeError;

namespace {
const std::chrono::seconds kDecodeTimeout{5};
}

namespace proxygen {

const huffman::HuffTree& QPACKDecoder::getHuffmanTree() const {
  return huffman::huffTree();
}

QPACKDecoder::~QPACKDecoder() {
  while (!decodeRequests_.empty()) {
    auto dreq = decodeRequests_.begin();
    dreq->err = HPACK::DecodeError::CANCELLED;
    checkComplete(dreq);
  }
  // futures are left running but have DestructorCheck's
}

void QPACKDecoder::decodeControlStream(folly::io::Cursor& cursor,
                                       uint32_t totalBytes) {
  decodeRequests_.emplace_front(nullptr, totalBytes);
  auto dreq = decodeRequests_.begin();
  HPACKDecodeBuffer dbuf(getHuffmanTree(), cursor, totalBytes,
                         maxUncompressed_);
  while (!dreq->hasError() && !dbuf.empty()) {
    dreq->pending++;
    decodeHeaderControl(dbuf, dreq);
  }
  dreq->allSubmitted = !dreq->hasError();
  dreq->consumedBytes = dbuf.consumedBytes();
  checkComplete(dreq);
}


bool QPACKDecoder::decodeStreaming(
    Cursor& cursor,
    uint32_t totalBytes,
    HeaderCodec::StreamingCallback* streamingCb) {

  decodeRequests_.emplace_front(streamingCb, totalBytes);
  auto dreq = decodeRequests_.begin();
  HPACKDecodeBuffer dbuf(getHuffmanTree(), cursor, totalBytes,
                         maxUncompressed_);
  while (!dreq->hasError() && !dbuf.empty()) {
    dreq->pending++;
    decodeHeader(dbuf, dreq);
  }
  dreq->allSubmitted = !dreq->hasError();
  dreq->consumedBytes = dbuf.consumedBytes();
  // checkComplete also handles errors
  auto done = checkComplete(dreq);
  queuedBytes_ = pendingDecodeBytes_;
  for (const auto& dr: decodeRequests_) {
    queuedBytes_ += dr.decodedSize.uncompressed;
  }
  return done;
}

void QPACKDecoder::decodeHeader(HPACKDecodeBuffer& dbuf,
                                DecodeRequestHandle dreq) {
  uint8_t byte = dbuf.peek();
  if (byte & QPACK::INDEX_REF.instruction) {
    decodeIndexedHeader(dbuf, dreq);
  } else {
    // LITERAL_NO_INDEXING or LITERAL_INCR_INDEXING
    decodeLiteralHeader(dbuf, dreq);
  }
}

void QPACKDecoder::decodeIndexedHeader(HPACKDecodeBuffer& dbuf,
                                       DecodeRequestHandle dreq) {
  uint32_t index;
  bool isStaticIndex = dbuf.peek() & QPACK::STATIC_HEADER;
  dreq->err = dbuf.decodeInteger(QPACK::INDEX_REF.prefixLength, index);
  if (dreq->hasError()) {
    LOG(ERROR) << "Decode error decoding index err=" << dreq->err;
    return;
  }
  // validate the index
  if (index == 0 || !isValid(index)) {
    LOG(ERROR) << "received invalid index: " << index;
    dreq->err = HPACK::DecodeError::INVALID_INDEX;
    return;
  }
  if (isStaticIndex) {
    index = staticToGlobalIndex(index);
  } else {
    index = dynamicToGlobalIndex(index);
  }
  if (isStatic(index)) {
    emit(dreq, getStaticHeader(index));
  } else {
    getDynamicHeader(index)
      .then([this, dreq] (QPACKHeaderTable::DecodeResult res) {
          // TODO: memory
          emit(dreq, res.ref);
        })
      .onTimeout(kDecodeTimeout, [this, dreq] {
          dreq->err = HPACK::DecodeError::TIMEOUT;
          checkComplete(dreq);
        })
      .onError([] (folly::BrokenPromise&) {
          // means the header table is being deleted
          VLOG(4) << "Broken promise";
        })
      .onError([this, dreq] (const std::runtime_error&) {
          dreq->err = HPACK::DecodeError::INVALID_INDEX;
          checkComplete(dreq);
        });
  }
}

bool QPACKDecoder::isValid(uint32_t index) {
  if (!isStatic(index)) {
    // all dynamic indexes must be considered valid, since they might come out
    // of order
    return true;
  }
  return getStaticTable().isValid(globalToStaticIndex(index));
}


void QPACKDecoder::emit(DecodeRequestHandle dreq, const HPACKHeader& header) {
  // would be nice to std::move here
  CHECK(dreq->cb);
  dreq->cb->onHeader(header.name.get(), header.value);
  dreq->decodedSize.uncompressed += header.bytes();
  dreq->pending--;
  checkComplete(dreq);
}


bool QPACKDecoder::checkComplete(DecodeRequestHandle dreq) {
  if (dreq->pending == 0 && dreq->allSubmitted) {
    if (dreq->cb) {
      dreq->cb->onHeadersComplete(dreq->decodedSize);
    }
    decodeRequests_.erase(dreq);
    return true;
  } else if (dreq->hasError()) {
    if (dreq->cb) {
      dreq->cb->onDecodeError(hpack2headerCodecError(dreq->err));
    }
    decodeRequests_.erase(dreq);
    return true;
  }
  return false;
}

void QPACKDecoder::decodeLiteralHeader(HPACKDecodeBuffer& dbuf,
                                       DecodeRequestHandle dreq) {
  decodeLiteral(dbuf, dreq, QPACK::LITERAL.prefixLength, 0);
}

void QPACKDecoder::decodeHeaderControl(HPACKDecodeBuffer& dbuf,
                                       DecodeRequestHandle dreq) {
  uint8_t byte = dbuf.peek();
  bool indexing = byte & QPACK::INSERT.instruction;
  uint32_t newIndex = 0;
  if (indexing) {
    dreq->err = dbuf.decodeInteger(QPACK::INSERT.prefixLength, newIndex);
    if (dreq->hasError()) {
      LOG(ERROR) << "Decode error decoding newIndex err=" << dreq->err;
      return;
    }
  } else {
    // deletion
    decodeDelete(dbuf, dreq);
    return;
  }
  decodeLiteral(dbuf, dreq, QPACK::NAME_REF.prefixLength, newIndex);
}

void QPACKDecoder::decodeLiteral(HPACKDecodeBuffer& dbuf,
                                 DecodeRequestHandle dreq,
                                 uint8_t nameIndexPrefixLen,
                                 uint32_t newIndex) {
  uint8_t nameIndexStaticMask = static_cast<uint8_t>(1) << nameIndexPrefixLen;
  if (dbuf.empty()) {
    LOG(ERROR) << "Decode error underflow";
    dreq->err = HPACK::DecodeError::BUFFER_UNDERFLOW;
    return;
  }

  auto byte = dbuf.peek();
  bool staticName = (byte & nameIndexStaticMask);
  uint32_t nameIndex = 0;
  dreq->err = dbuf.decodeInteger(nameIndexPrefixLen, nameIndex);
  if (dreq->hasError()) {
    LOG(ERROR) << "Decode error decoding index err=" << dreq->err;
    return;
  }
  QPACKHeaderTable::DecodeFuture nameFuture{
    QPACKHeaderTable::DecodeResult(HPACKHeader())};
  if (nameIndex) {
    if (staticName) {
      nameIndex = staticToGlobalIndex(nameIndex);
    } else {
      nameIndex = dynamicToGlobalIndex(nameIndex);
    }
    nameFuture = getHeader(nameIndex);
  } else {
    folly::fbstring headerName;
    dreq->err = dbuf.decodeLiteral(headerName);
    HPACKHeader header(headerName, "");
    if (dreq->hasError()) {
      LOG(ERROR) << "Error decoding header name err=" << dreq->err;
      return;
    }
    // This path might be a little inefficient in the name of symmetry below
    nameFuture = folly::makeFuture<QPACKHeaderTable::DecodeResult>(
      QPACKHeaderTable::DecodeResult(std::move(header)));
  }
  // value
  folly::fbstring value;
  dreq->err = dbuf.decodeLiteral(value);
  if (dreq->hasError()) {
    LOG(ERROR) << "Error decoding header value name=" <<
      ((nameFuture.isReady()) ?
       nameFuture.value().ref.name.get() :
       folly::to<string>("pending=", nameIndex)) <<
      " err=" << dreq->err;
    return;
  }

  pendingDecodeBytes_ += value.length();
  nameFuture
    .then(
      [this, value=std::move(value), dreq, newIndex]
      (QPACKHeaderTable::DecodeResult res) mutable {
        pendingDecodeBytes_ -= value.length();
        HPACKHeader header;
        if (res.which == 1) {
          header.name = std::move(res.value.name);
        } else {
          header.name = res.ref.name;
        }
        header.value = std::move(value);
        // get the memory story straight
        if (newIndex) {
          table_.add(header, newIndex);
        } else {
          emit(dreq, header);
        }
      })
    .onTimeout(kDecodeTimeout, [this, dreq] {
        dreq->err = HPACK::DecodeError::TIMEOUT;
        checkComplete(dreq);
      })
    .onError([] (folly::BrokenPromise&) {
        // means the header table is being deleted
        VLOG(4) << "Broken promise";
      })
    .onError([this, dreq] (const std::runtime_error&) {
        dreq->err = HPACK::DecodeError::INVALID_INDEX;
        checkComplete(dreq);
      });
}

void QPACKDecoder::decodeDelete(HPACKDecodeBuffer& dbuf,
                                DecodeRequestHandle dreq) {
  uint32_t refcount;
  uint32_t delIndex;
  dreq->err = dbuf.decodeInteger(QPACK::DELETE.prefixLength, refcount);
  if (dreq->hasError() || refcount == 0) {
    LOG(ERROR) << "Invalid recount decoding delete refcount=" << refcount;
    return;
  }
  dreq->err = dbuf.decodeInteger(QPACK::NONE.prefixLength, delIndex);
  if (dreq->hasError() || delIndex == 0 || isStatic(delIndex)) {
    LOG(ERROR) << "Invalid index decoding delete delIndex=" << delIndex;
    return;
  }

  // no need to hold this request
  dreq->pending--;
  table_.decoderRemove(globalToDynamicIndex(delIndex), refcount)
    .then([this, delIndex] {
        VLOG(4) << "delete complete for delIndex=" << delIndex;
        callback_.ack(globalToDynamicIndex(delIndex) - 1);
      })
    .onTimeout(kDecodeTimeout, [this, delIndex] {
        LOG(ERROR) << "Timeout trying to delete delIndex=" << delIndex;
        callback_.onError();
      })
    .onError([this, delIndex] (const std::runtime_error&) {
        LOG(ERROR) << "Decode error trying to delete delIndex=" << delIndex;
        callback_.onError();
      });

}
}
