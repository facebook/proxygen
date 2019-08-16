#include <proxygen/httpserver/samples/hq/HQLoggerHelper.h>

using namespace quic::samples;

HQLoggerHelper::HQLoggerHelper(const std::string& path, bool pretty)
    : outputPath_(path), pretty_(pretty) {
}

HQLoggerHelper::~HQLoggerHelper() {
  try {
    outputLogsToFile(outputPath_, pretty_);
  } catch (...) {
  }
}
