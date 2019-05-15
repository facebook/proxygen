/*
 *  Copyright (c) 2019-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/transport/PersistentFizzPskCache.h>

#include <fizz/record/Types.h>

using namespace folly;

namespace proxygen {

std::string serializePsk(const fizz::client::CachedPsk& psk) {
  uint64_t ticketIssueTime =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          psk.ticketIssueTime.time_since_epoch())
          .count();
  uint64_t ticketExpirationTime =
      std::chrono::duration_cast<std::chrono::seconds>(
          psk.ticketExpirationTime.time_since_epoch())
          .count();

  auto serialized = IOBuf::create(0);
  io::Appender appender(serialized.get(), 512);
  fizz::detail::writeBuf<uint16_t>(
      folly::IOBuf::wrapBuffer(StringPiece(psk.psk)), appender);
  fizz::detail::writeBuf<uint16_t>(
      folly::IOBuf::wrapBuffer(StringPiece(psk.secret)), appender);
  fizz::detail::write(psk.version, appender);
  fizz::detail::write(psk.cipher, appender);
  if (psk.group.hasValue()) {
    fizz::detail::write(static_cast<uint8_t>(1), appender);
    fizz::detail::write(*psk.group, appender);
  } else {
    fizz::detail::write(static_cast<uint8_t>(0), appender);
  }
  fizz::detail::writeBuf<uint8_t>(
      psk.alpn ? folly::IOBuf::wrapBuffer(StringPiece(*psk.alpn)) : nullptr,
      appender);
  fizz::detail::write(psk.ticketAgeAdd, appender);
  fizz::detail::write(ticketIssueTime, appender);
  fizz::detail::write(ticketExpirationTime, appender);
  ssl::X509UniquePtr x509(psk.serverCert ? psk.serverCert->getX509() : nullptr);
  fizz::detail::writeBuf<uint32_t>(
      x509 ? folly::ssl::OpenSSLCertUtils::derEncode(*x509) : nullptr,
      appender);
  x509 = psk.clientCert ? psk.clientCert->getX509() : nullptr;
  fizz::detail::writeBuf<uint32_t>(
      x509 ? folly::ssl::OpenSSLCertUtils::derEncode(*x509) : nullptr,
      appender);
  fizz::detail::write(psk.maxEarlyDataSize, appender);

  return serialized->moveToFbString().toStdString();
}

fizz::client::CachedPsk deserializePsk(const std::string& str,
                                       const fizz::Factory& factory) {
  auto buf = IOBuf::wrapBuffer(str.data(), str.length());
  io::Cursor cursor(buf.get());
  fizz::client::CachedPsk psk;
  psk.type = fizz::PskType::Resumption;

  std::unique_ptr<IOBuf> pskData;
  fizz::detail::readBuf<uint16_t>(pskData, cursor);
  psk.psk = pskData->moveToFbString().toStdString();

  std::unique_ptr<IOBuf> secretData;
  fizz::detail::readBuf<uint16_t>(secretData, cursor);
  psk.secret = secretData->moveToFbString().toStdString();

  fizz::detail::read(psk.version, cursor);
  fizz::detail::read(psk.cipher, cursor);
  uint8_t hasGroup;
  fizz::detail::read(hasGroup, cursor);
  if (hasGroup == 1) {
    fizz::NamedGroup group;
    fizz::detail::read(group, cursor);
    psk.group = group;
  }

  std::unique_ptr<IOBuf> alpnData;
  fizz::detail::readBuf<uint8_t>(alpnData, cursor);
  if (!alpnData->empty()) {
    psk.alpn = alpnData->moveToFbString().toStdString();
  }

  fizz::detail::read(psk.ticketAgeAdd, cursor);

  uint64_t ticketIssueTime;
  fizz::detail::read(ticketIssueTime, cursor);
  psk.ticketIssueTime = std::chrono::time_point<std::chrono::system_clock>(
      std::chrono::milliseconds(ticketIssueTime));

  uint64_t ticketExpirationTime;
  fizz::detail::read(ticketExpirationTime, cursor);
  psk.ticketExpirationTime = std::chrono::time_point<std::chrono::system_clock>(
      std::chrono::seconds(ticketExpirationTime));

  std::unique_ptr<IOBuf> certData;
  fizz::detail::readBuf<uint32_t>(certData, cursor);
  if (!certData->empty()) {
    psk.serverCert = factory.makePeerCert(std::move(certData));
  }

  std::unique_ptr<IOBuf> clientCertData;
  fizz::detail::readBuf<uint32_t>(clientCertData, cursor);
  if (!clientCertData->empty()) {
    psk.clientCert = factory.makePeerCert(std::move(clientCertData));
  }

  fizz::detail::read(psk.maxEarlyDataSize, cursor);

  return psk;
}
}

namespace folly {

template <>
dynamic toDynamic(const proxygen::PersistentCachedPsk& cached) {
  dynamic d = dynamic::object;
  d["psk"] = cached.serialized;
  d["uses"] = cached.uses;
  return d;
}

template <>
proxygen::PersistentCachedPsk convertTo(const dynamic& d) {
  proxygen::PersistentCachedPsk psk;
  psk.serialized = d["psk"].asString();
  psk.uses = folly::to<size_t>(d["uses"].asInt());
  return psk;
}
}
