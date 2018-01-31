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

#include <memory>
#include <proxygen/lib/http/codec/TransportDirection.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMDecoder.h>
#include <proxygen/lib/http/codec/compress/experimental/qcram/QCRAMEncoder.h>
#include <proxygen/lib/http/codec/compress/HeaderIndexingStrategy.h>
#include <proxygen/lib/http/codec/compress/HeaderCodec.h>
#include <string>
#include <vector>

namespace folly { namespace io {
class Cursor;
}}

namespace proxygen {

class QCRAMHeader;

/*
 * Struct to hold the encoder and decoder information
 */
struct QCRAMTableInfo {
  // Egress table info (encoder)
  uint32_t egressHeaderTableSize_{0};
  uint32_t egressBytesStored_{0};
  uint32_t egressHeadersStored_{0};

  // Ingress table info (decoder)
  uint32_t ingressHeaderTableSize_{0};
  uint32_t ingressBytesStored_{0};
  uint32_t ingressHeadersStored_{0};

  QCRAMTableInfo(uint32_t egressHeaderTableSize,
                 uint32_t egressBytesStored,
                 uint32_t egressHeadersStored,
                 uint32_t ingressHeaderTableSize,
                 uint32_t ingressBytesStored,
                 uint32_t ingressHeadersStored) :
      egressHeaderTableSize_(egressHeaderTableSize),
      egressBytesStored_(egressBytesStored),
      egressHeadersStored_(egressHeadersStored),
      ingressHeaderTableSize_(ingressHeaderTableSize),
      ingressBytesStored_(ingressBytesStored),
      ingressHeadersStored_(ingressHeadersStored) {}

  QCRAMTableInfo() {}

  bool operator==(const QCRAMTableInfo& tableInfo) const {
    return egressHeaderTableSize_ == tableInfo.egressHeaderTableSize_ &&
           egressBytesStored_ == tableInfo.egressBytesStored_ &&
           egressHeadersStored_ == tableInfo.egressHeadersStored_ &&
           ingressHeaderTableSize_ == tableInfo.ingressHeaderTableSize_ &&
           ingressBytesStored_ == tableInfo.ingressBytesStored_ &&
           ingressHeadersStored_ == tableInfo.ingressHeadersStored_;
  }
};

/*
 * Current version of the wire protocol. When we're making changes to the wire
 * protocol we need to change this version and the NPN string so that old
 * clients will not be able to negotiate it anymore.
 */

class QCRAMCodec : public HeaderCodec, HeaderCodec::StreamingCallback {
 public:
  explicit QCRAMCodec(TransportDirection direction,
                      bool emitSequenceNumbers = false,
                      bool useBaseIndex = false,
                      bool autoCommit = true);
  ~QCRAMCodec() override {}

  std::unique_ptr<folly::IOBuf> encode(
    std::vector<compress::Header>&) noexcept override {
    LOG(FATAL) << "deprecated: use encodeQuic instead";
    return nullptr;
  }

  QCRAMEncoder::EncodeResult encode(
      bool newPacket,
      std::vector<compress::Header>& headers) noexcept;

  Result<HeaderDecodeResult, HeaderDecodeError>
  decode(folly::io::Cursor& cursor, uint32_t length) noexcept override;

  // Callbacks that handle Codec-level stats and errors
  void onHeader(const folly::fbstring& name,
                const folly::fbstring& value) override;
  void onHeadersComplete(HTTPHeaderSize decodedSize) override;
  void onDecodeError(HeaderDecodeError decodeError) override;

  void setDecoderFrameFlags(FrameFlags flags) {
    decoder_.setFrameFlags(flags);
  }

  void decodeStreaming(
      folly::io::Cursor& cursor,
      uint32_t length,
      HeaderCodec::StreamingCallback* streamingCb) noexcept override;

  void setEncoderHeaderTableSize(uint32_t size) {
    encoder_.setHeaderTableSize(size);
  }

  void setDecoderHeaderTableMaxSize(uint32_t size) {
    decoder_.setHeaderTableMaxSize(size);
  }

  void setCommitEpoch(uint16_t commitEpoch) {
    encoder_.setCommitEpoch(commitEpoch);
  }


  void describe(std::ostream& os) const;

  void setMaxUncompressed(uint32_t maxUncompressed) override {
    HeaderCodec::setMaxUncompressed(maxUncompressed);
    decoder_.setMaxUncompressed(maxUncompressed);
  }

  QCRAMTableInfo getQCRAMTableInfo() const {
    return QCRAMTableInfo(encoder_.getTableSize(),
                          encoder_.getBytesStored(),
                          encoder_.getHeadersStored(),
                          decoder_.getTableSize(),
                          decoder_.getBytesStored(),
                          decoder_.getHeadersStored());
  }

  // Used for QCRAM simulation
  void packetFlushed() {
    encoder_.packetFlushed();
  }

  void setHeaderIndexingStrategy(const HeaderIndexingStrategy* indexingStrat) {
    encoder_.setHeaderIndexingStrategy(indexingStrat);
  }
  const HeaderIndexingStrategy* getHeaderIndexingStrategy() const {
    return encoder_.getHeaderIndexingStrategy();
  }

  bool dependsOK(uint32_t depends) const {
    return decoder_.dependsOK(depends);
  }

  void recvAck(uint16_t seqn) {
    encoder_.recvAck(seqn);
  }

 protected:
  QCRAMEncoder encoder_;
  QCRAMDecoder decoder_;

 private:
  std::vector<QCRAMHeader> decodedHeaders_;
};

std::ostream& operator<<(std::ostream& os, const QCRAMCodec& codec);
}
