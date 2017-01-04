/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/codec/HTTPSettings.h>

namespace proxygen {

void HTTPSettings::setSetting(SettingsId id, uint32_t val) {
  auto s = findSetting(id);
  if (!s) {
    // Create the setting
    settings_.emplace_back(
      id, val);
    ++numSettings_;
  } else {
    // Enable and/or update the setting
    if (!s->isSet) {
      s->isSet = true;
      ++numSettings_;
    }
    s->value = val;
  }
}

void HTTPSettings::unsetSetting(SettingsId id) {
  auto s = findSetting(id);
  if (s && s->isSet) {
    s->isSet = false;
    --numSettings_;
  }
}

const HTTPSetting* HTTPSettings::getSetting(SettingsId id) const {
  auto ret = findSettingConst(id);
  if (!ret || !ret->isSet) {
    return nullptr;
  }
  return ret;
}

uint32_t HTTPSettings::getSetting(SettingsId id,
                                  uint32_t defaultValue) const {
  auto ret = findSettingConst(id);
  if (!ret || !ret->isSet) {
    return defaultValue;
  }
  return ret->value;
}

HTTPSetting* HTTPSettings::findSetting(SettingsId id) {
  for (auto& setting: settings_) {
    if (setting.id == id) {
      return &setting;
    }
  }
  return nullptr;
}

const HTTPSetting* HTTPSettings::findSettingConst(SettingsId id) const {
  for (auto& setting: settings_) {
    if (setting.id == id) {
      return &setting;
    }
  }
  return nullptr;
}

}
