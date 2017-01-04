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

#include <initializer_list>
#include <proxygen/lib/http/codec/SettingsId.h>
#include <vector>

namespace proxygen {

struct HTTPSetting {
  HTTPSetting(SettingsId i,
              uint32_t v):
      id(i),
      value(v),
      isSet(true) {}

  SettingsId id;
  uint32_t value;
  bool isSet;
};

class HTTPSettings {
 public:
  // HTTP/2 Defaults
  HTTPSettings() :
      settings_(
        {{ SettingsId::HEADER_TABLE_SIZE, 4096 },
         { SettingsId::ENABLE_PUSH, 1 },
         { SettingsId::MAX_FRAME_SIZE, 16384 }}),
      numSettings_(3) {
  }
  HTTPSettings(
    const std::initializer_list<SettingPair>& initialSettings)
      : numSettings_(0) {
    for (auto& setting: initialSettings) {
      setSetting(setting.first, setting.second);
    }
  }
  void setSetting(SettingsId id, uint32_t val);
  void unsetSetting(SettingsId id);
  const HTTPSetting* getSetting(SettingsId id) const;
  uint32_t getSetting(SettingsId id, uint32_t defaultVal) const;
  // Note: this does not count disabled settings
  uint8_t getNumSettings() const { return numSettings_; }
  // The length of the returned vector may be greater than getNumSettings()
  // TODO: replace this with an iterator that skips disabled settings
  const std::vector<HTTPSetting>& getAllSettings() { return settings_; }
  void clearSettings() {
    settings_.clear();
    numSettings_ = 0;
  }
 private:
  HTTPSetting* findSetting(SettingsId id);
  const HTTPSetting* findSettingConst(SettingsId id) const;

  std::vector<HTTPSetting> settings_;
  uint8_t numSettings_{0};
};

typedef std::vector<HTTPSetting> SettingsList;

}
