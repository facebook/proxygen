/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/compress/Logging.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

using std::ostream;
using std::string;
using std::stringstream;
using std::vector;

namespace proxygen {

ostream& operator<<(ostream& os, const folly::IOBuf* buf) {
  const uint8_t* data = buf->data();
  char tmp[24];
  for (size_t i = 0; i < buf->length(); i++) {
    sprintf(tmp, "%02x", data[i]);
    os << tmp;
    if ((i + 1) % 2 == 0) {
      os << ' ';
    }
    if ((i + 1) % 16 == 0) {
      os << std::endl;
    }
  }
  return os;
}

ostream& operator<<(ostream& os, const std::list<uint32_t>* refset) {
  os << std::endl << '[';
  for (auto& ref : *refset) {
    os << ref << ' ';
  }
  os << ']' << std::endl;
  return os;
}

std::ostream& operator<<(std::ostream& os, const std::vector<HPACKHeader>& v) {
  for (const auto &h : v) {
    os << h.name << ": " << h.value << std::endl;
  }
  os << std::endl;
  return os;
}

void dumpBinToFile(const std::string& filename, const folly::IOBuf* buf) {
  struct stat fstat;
  bool exists = (stat(filename.c_str(), &fstat) == 0);
  if (exists) {
    // don't write anything if the file exists
    return;
  }
  std::ofstream file(filename, std::ofstream::binary);
  if (!file.is_open()) {
    LOG(ERROR) << "cannot open file " << filename;
    return;
  }
  if (!buf) {
    file.close();
    return;
  }
  const folly::IOBuf* first = buf;
  do {
    file.write((const char *)buf->data(), buf->length());
    buf = buf->next();
  } while (buf != first);
  file.close();
  LOG(INFO) << "wrote chain " << dumpChain(buf) << " to " << filename;
}

string dumpChain(const folly::IOBuf* buf) {
  stringstream out;
  auto b = buf;
  do {
    out << "iobuf of size " << b->length()
        << " tailroom " << b->tailroom();
    b = b->next();
  } while (b != buf);
  return out.str();
}

string dumpBin(const folly::IOBuf* buf, uint8_t bytesPerLine) {
  string out;
  const folly::IOBuf* first = buf;
  if (!buf) {
    return out;
  }
  do {
    const uint8_t* data = buf->data();
    for (size_t i = 0; i < buf->length(); i++) {
      for (int b = 7; b >= 0; b--) {
        out += data[i] & 1 << b ? '1' : '0';
      }
      out += ' ';
      out += isprint(data[i]) ? data[i] : ' ';
      if ((i + 1) % bytesPerLine == 0) {
        out += '\n';
      } else {
        out += ' ';
      }
    }
    out += '\n';
    buf = buf->next();
  } while (buf != first);
  return out;
}

string printDelta(const vector<HPACKHeader> &v1,
                  const vector<HPACKHeader> &v2) {
  stringstream out;
  // similar with merge operation
  size_t i = 0;
  size_t j = 0;
  out << std::endl;
  while (i < v1.size() && j < v2.size()) {
    if (v1[i] < v2[j]) {
      if (i > 0 && v1[i - 1] == v1[i]) {
        out << " duplicate " << v1[i] << std::endl;
      } else {
        out << " + " << v1[i] << std::endl;
      }
      i++;
    } else if (v1[i] > v2[j]) {
      out << " - " << v2[j] << std::endl;
      j++;
    } else {
      i++;
      j++;
    }
  }
  while (i < v1.size()) {
    out << " + " << v1[i];
    if (i > 0 && v1[i - 1] == v1[i]) {
      out << " (duplicate)";
    }
    out << std::endl;
    i++;
  }
  while (j < v2.size()) {
    out << " - " << v2[j] << std::endl;
    j++;
  }
  return out.str();
}

}
