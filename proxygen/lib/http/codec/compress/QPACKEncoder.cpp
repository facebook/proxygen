/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/QPACKEncoder.h>
#include <proxygen/lib/http/codec/compress/HPACKDecodeBuffer.h>

using std::vector;

namespace proxygen {

QPACKEncoder::QPACKEncoder(bool huffman, uint32_t tableSize) :
    // We only need the 'QPACK' table if we are using base index
    HPACKEncoderBase(huffman),
    QPACKContext(tableSize, true),
    controlBuffer_(kBufferGrowth, huffman) {
  // Default the encoder indexing strategy; it can be updated later as well
  setHeaderIndexingStrategy(HeaderIndexingStrategy::getDefaultInstance());
}

QPACKEncoder::EncodeResult
QPACKEncoder::encode(const vector<HPACKHeader>& headers,
                     uint32_t headroom,
                     uint64_t streamId,
                     uint32_t maxEncoderStreamBytes) {
  if (headroom) {
    streamBuffer_.addHeadroom(headroom);
  }
  maxEncoderStreamBytes_ = maxEncoderStreamBytes;
  maxEncoderStreamBytes_ -=
    handlePendingContextUpdate(controlBuffer_, table_.capacity());
  return encodeQ(headers, streamId);
}

QPACKEncoder::EncodeResult
QPACKEncoder::encodeQ(const vector<HPACKHeader>& headers, uint64_t streamId) {
  OutstandingBlock outstandingBlock;
  // curOutstanding_ points to a local stack variable, it's mostly for
  // convenience so other methods invoked from here can access it.
  curOutstanding_ = &outstandingBlock;
  auto baseIndex = table_.getBaseIndex();

  uint32_t largestReference = 0;
  for (const auto& header: headers) {
    encodeHeaderQ(header, baseIndex, &largestReference);
  }

  auto streamBlock = streamBuffer_.release();

  // encode the prefix
  if (largestReference == 0) {
    streamBuffer_.encodeInteger(0); // LR
    streamBuffer_.encodeInteger(0); // baseIndex
  } else {
    auto wireLR = (largestReference % (2 * table_.getMaxEntries())) + 1;
    streamBuffer_.encodeInteger(wireLR);
    if (largestReference > baseIndex) {
      streamBuffer_.encodeInteger(largestReference - baseIndex,
                                  HPACK::Q_DELTA_BASE_NEG,
                                  HPACK::Q_DELTA_BASE.prefixLength);
    } else {
      streamBuffer_.encodeInteger(baseIndex - largestReference,
                                  HPACK::Q_DELTA_BASE_POS,
                                  HPACK::Q_DELTA_BASE.prefixLength);
    }
  }
  auto streamBuffer = streamBuffer_.release();
  streamBuffer->prependChain(std::move(streamBlock));

  auto controlBuf = controlBuffer_.release();
  // curOutstanding_.references could be empty, if the block encodes only static
  // headers and/or literals.  If so we don't track anything.
  if (!curOutstanding_->references.empty()) {
    if (curOutstanding_->vulnerable) {
      DCHECK(allowVulnerable());
      numVulnerable_++;
    }
    outstanding_[streamId].emplace_back(std::move(outstandingBlock));
  }
  // Clear the pointer to our stack
  curOutstanding_ = nullptr;

  return { std::move(controlBuf), std::move(streamBuffer) };
}

void QPACKEncoder::encodeHeaderQ(
  const HPACKHeader& header, uint32_t baseIndex, uint32_t* largestReference) {
  uint32_t index = getStaticTable().getIndex(header);
  if (index > 0) {
    // static reference
    streamBuffer_.encodeInteger(index - 1,
                                HPACK::Q_INDEXED.code | HPACK::Q_INDEXED_STATIC,
                                HPACK::Q_INDEXED.prefixLength);
    return;
  }

  bool indexable = shouldIndex(header);
  if (indexable) {
    index = table_.getIndex(header, allowVulnerable());
    if (index == QPACKHeaderTable::UNACKED) {
      index = 0;
      indexable = false;
    }
  }
  if (index != 0) {
    // dynamic reference
    bool duplicated = false;
    std::tie(duplicated, index) = maybeDuplicate(index);
    // index is now 0 or absolute
    indexable &= (duplicated && index == 0);
  }
  if (index == 0) {
    // No valid entry matching header, see if there's a matching name
    uint32_t nameIndex = 0;
    uint32_t absoluteNameIndex = 0;
    bool isStaticName = false;
    std::tie(isStaticName, nameIndex, absoluteNameIndex) =
      getNameIndexQ(header.name);

    // Now check if we should emit an insertion on the control stream
    // Don't try to index if we're out of encoder flow control
    indexable &= maxEncoderStreamBytes_ > 0;
    if (indexable && table_.canIndex(header)) {
      encodeInsertQ(header, isStaticName, nameIndex);
      CHECK(table_.add(header.copy()));
      if (allowVulnerable() && lastEntryAvailable()) {
        index = table_.getBaseIndex();
      } else {
        index = 0;
        if (absoluteNameIndex > 0 &&
            !table_.isValid(table_.absoluteToRelative(absoluteNameIndex))) {
          // The insert may have invalidated the name index.
          isStaticName = true;
          nameIndex = 0;
          absoluteNameIndex = 0;
        }
      }
    }

    if (index == 0) {
      // Couldn't insert it: table full, not indexable, or table contains
      // vulnerable reference.  Encode a literal on the request stream.
      encodeStreamLiteralQ(header, isStaticName, nameIndex, absoluteNameIndex,
                           baseIndex, largestReference);
      return;
    }
  }

  // Encoding a dynamic index reference
  DCHECK_NE(index, 0);
  trackReference(index, largestReference);
  if (index > baseIndex) {
    streamBuffer_.encodeInteger(index - baseIndex - 1, HPACK::Q_INDEXED_POST);
  } else {
    streamBuffer_.encodeInteger(baseIndex - index, HPACK::Q_INDEXED);
  }
}

bool QPACKEncoder::shouldIndex(const HPACKHeader& header) const {
  return (header.bytes() <= table_.capacity()) &&
    (!indexingStrat_ || indexingStrat_->indexHeader(header));
}

std::pair<bool, uint32_t> QPACKEncoder::maybeDuplicate(
      uint32_t relativeIndex) {
  auto res = table_.maybeDuplicate(relativeIndex, allowVulnerable());
  if (res.first) {
    VLOG(4) << "Encoded duplicate index=" << relativeIndex;
    encodeDuplicate(relativeIndex);
    // Note we will emit duplications even when we are out of flow control,
    // but we won't reference them (eg: like we were at vulnerable max).
    if (!lastEntryAvailable()) {
      VLOG(4) << "Duplicate is not usable because it overran encoder flow "
        "control";
      return {true, 0};
    }
  }
  return res;
}

std::tuple<bool, uint32_t, uint32_t> QPACKEncoder::getNameIndexQ(
  const HPACKHeaderName& headerName) {
  uint32_t absoluteNameIndex = 0;
  uint32_t nameIndex = getStaticTable().nameIndex(headerName);
  bool isStatic = true;
  if (nameIndex == 0) {
    // check dynamic table
    nameIndex = table_.nameIndex(headerName, allowVulnerable());
    if (nameIndex != 0) {
      absoluteNameIndex = maybeDuplicate(nameIndex).second;
      if (absoluteNameIndex) {
        isStatic = false;
        nameIndex = table_.absoluteToRelative(absoluteNameIndex);
      } else {
        nameIndex = 0;
        absoluteNameIndex = 0;
      }
    }
  }
  return std::tuple<bool, uint32_t, uint32_t>(
    isStatic, nameIndex, absoluteNameIndex);
}

void QPACKEncoder::encodeStreamLiteralQ(
  const HPACKHeader& header, bool isStaticName, uint32_t nameIndex,
  uint32_t absoluteNameIndex, uint32_t baseIndex, uint32_t* largestReference) {
  if (absoluteNameIndex > 0) {
    // Dynamic name reference, vulnerability checks already done
    CHECK(absoluteNameIndex <= baseIndex || allowVulnerable());
    trackReference(absoluteNameIndex, largestReference);
  }
  if (absoluteNameIndex > baseIndex) {
    encodeLiteralQ(header,
                   false, /* not static */
                   true, /* post base */
                   absoluteNameIndex - baseIndex,
                   HPACK::Q_LITERAL_NAME_REF_POST);
  } else {
    encodeLiteralQ(header,
                   isStaticName,
                   false, /* not post base */
                   isStaticName ? nameIndex : baseIndex - absoluteNameIndex + 1,
                   HPACK::Q_LITERAL_NAME_REF);
  }
}

void QPACKEncoder::trackReference(uint32_t absoluteIndex,
                                  uint32_t* largestReference) {
  CHECK_NE(absoluteIndex, 0);
  CHECK(curOutstanding_);
  if (absoluteIndex > *largestReference) {
    *largestReference = absoluteIndex;
    if (table_.isVulnerable(absoluteIndex)) {
      curOutstanding_->vulnerable = true;
    }
  }
  auto res = curOutstanding_->references.insert(absoluteIndex);
  if (res.second) {
    VLOG(5) << "Bumping refcount for absoluteIndex=" << absoluteIndex;
    table_.addRef(absoluteIndex);
  }
}

void QPACKEncoder::encodeDuplicate(uint32_t index) {
  DCHECK_GT(index, 0);
  maxEncoderStreamBytes_ -=
    controlBuffer_.encodeInteger(index - 1, HPACK::Q_DUPLICATE);
}

void QPACKEncoder::encodeInsertQ(const HPACKHeader& header,
                                 bool isStaticName,
                                 uint32_t nameIndex) {
  auto encoded = encodeLiteralQHelper(
      controlBuffer_, header, isStaticName, nameIndex,
      HPACK::Q_INSERT_NAME_REF_STATIC, HPACK::Q_INSERT_NAME_REF,
      HPACK::Q_INSERT_NO_NAME_REF);
  maxEncoderStreamBytes_ -= encoded;
}

void QPACKEncoder::encodeLiteralQ(const HPACKHeader& header,
                                  bool isStaticName,
                                  bool postBase,
                                  uint32_t nameIndex,
                                  const HPACK::Instruction& idxInstr) {
  DCHECK(!isStaticName || !postBase);
  encodeLiteralQHelper(
      streamBuffer_, header, isStaticName, nameIndex,
      HPACK::Q_LITERAL_STATIC, idxInstr,
      HPACK::Q_LITERAL);
}

uint32_t QPACKEncoder::encodeLiteralQHelper(
    HPACKEncodeBuffer& buffer,
    const HPACKHeader& header,
    bool isStaticName,
    uint32_t nameIndex,
    uint8_t staticFlag,
    const HPACK::Instruction& idxInstr,
    const HPACK::Instruction& litInstr) {
  uint32_t encoded = 0;
  // name
  if (nameIndex) {
    VLOG(10) << "encoding name index=" << nameIndex;
    DCHECK_NE(nameIndex, QPACKHeaderTable::UNACKED);
    nameIndex -= 1; // we already know it's not 0
    uint8_t byte = idxInstr.code;
    if (isStaticName) {
      byte |= staticFlag;
    }
    encoded += buffer.encodeInteger(nameIndex, byte, idxInstr.prefixLength);
  } else {
    encoded += buffer.encodeLiteral(litInstr.code, litInstr.prefixLength,
                                    header.name.get());
  }
  // value
  encoded += buffer.encodeLiteral(header.value);
  return encoded;
}

HPACK::DecodeError QPACKEncoder::decodeDecoderStream(
    std::unique_ptr<folly::IOBuf> buf) {
  decoderIngress_.append(std::move(buf));
  folly::io::Cursor cursor(decoderIngress_.front());
  HPACKDecodeBuffer dbuf(cursor, decoderIngress_.chainLength(), 0);
  HPACK::DecodeError err = HPACK::DecodeError::NONE;
  uint32_t consumed = 0;
  while (err == HPACK::DecodeError::NONE && !dbuf.empty()) {
    consumed = dbuf.consumedBytes();
    auto byte = dbuf.peek();
    if (byte & HPACK::Q_HEADER_ACK.code) {
      err = decodeHeaderAck(dbuf, HPACK::Q_HEADER_ACK.prefixLength, false);
    } else if (byte & HPACK::Q_CANCEL_STREAM.code) {
      err = decodeHeaderAck(dbuf, HPACK::Q_CANCEL_STREAM.prefixLength, true);
    } else { // TABLE_STATE_SYNC
      uint64_t inserts = 0;
      err = dbuf.decodeInteger(HPACK::Q_TABLE_STATE_SYNC.prefixLength, inserts);
      if (err == HPACK::DecodeError::NONE) {
        err = onTableStateSync(inserts);
      } else if (err != HPACK::DecodeError::BUFFER_UNDERFLOW) {
        LOG(ERROR) << "Failed to decode num inserts, err=" << err;
      }
    }
  } // while
  if (err == HPACK::DecodeError::BUFFER_UNDERFLOW) {
    err = HPACK::DecodeError::NONE;
    decoderIngress_.trimStart(consumed);
  } else {
    decoderIngress_.trimStart(dbuf.consumedBytes());
  }
  return err;
}

HPACK::DecodeError QPACKEncoder::decodeHeaderAck(HPACKDecodeBuffer& dbuf,
                                                 uint8_t prefixLength,
                                                 bool all) {
  uint64_t streamId = 0;
  auto err = dbuf.decodeInteger(prefixLength, streamId);
  if (err == HPACK::DecodeError::NONE) {
    err = onHeaderAck(streamId, all);
  } else if (err != HPACK::DecodeError::BUFFER_UNDERFLOW) {
    LOG(ERROR) << "Failed to decode streamId, err=" << err;
  }
  return err;
}

HPACK::DecodeError QPACKEncoder::onTableStateSync(uint32_t inserts) {
  if (inserts == 0 || !table_.onTableStateSync(inserts)) {
    return HPACK::DecodeError::INVALID_ACK;
  }
  return HPACK::DecodeError::NONE;
}

HPACK::DecodeError QPACKEncoder::onHeaderAck(uint64_t streamId, bool all) {
  auto it = outstanding_.find(streamId);
  if (it == outstanding_.end()) {
    if (!all) {
      LOG(ERROR) << "Received an ack with no outstanding header blocks stream="
                 << streamId;
      return HPACK::DecodeError::INVALID_ACK;
    } else {
      // all implies a reset, meaning it's not an error if there are no
      // outstanding blocks
      return HPACK::DecodeError::NONE;
    }
  }
  DCHECK(!it->second.empty()) << "Invariant violation: no blocks in stream "
     "record";
  VLOG(5) << ((all) ? "onCancelStream" : "onHeaderAck") << " streamId="
          << streamId;
  if (all) {
    // Happens when a stream is reset
    for (auto& block: it->second) {
      for (auto i: block.references) {
        VLOG(5) << "Decrementing refcount for absoluteIndex=" << i;
        table_.subRef(i);
      }
      if (block.vulnerable) {
        numVulnerable_--;
      }
    }
    it->second.clear();
  } else {
    auto block = std::move(it->second.front());
    it->second.pop_front();
    // a different stream, sub all the references
    for (auto i: block.references) {
      VLOG(5) << "Decrementing refcount for absoluteIndex=" << i;
      table_.subRef(i);
    }
    if (block.vulnerable) {
      numVulnerable_--;
    }
    // largest reference is implicitly acknowledged
    if (!block.references.empty()) {
      auto largestReference = *block.references.rbegin();
      VLOG(5) << "Implicitly acknowledging absoluteIndex=" << largestReference;
      table_.setMaxAcked(largestReference);
    }
  }
  if (it->second.empty()) {
    outstanding_.erase(it);
  }
  return HPACK::DecodeError::NONE;
}

}
