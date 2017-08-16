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

#include <algorithm>

namespace proxygen {

void HTTPSettings::setSetting(SettingsId id, uint32_t val) {
  auto iter = getSettingIter(id);
  if (iter != std::end(settings_)) {
    (*iter).value = val;
  } else {
    settings_.emplace_back(id, val);
  }
}

void HTTPSettings::unsetSetting(SettingsId id) {
  std::ptrdiff_t index = std::distance(
    std::begin(settings_), getSettingIter(id));
  // Casting to size_t as its guaranteed that we will have a positive offset
  if ((std::size_t)index < settings_.size()) {
    // If we have an index but there is only one element in the setting vector
    // we can jump straight to the pop_back
    if (settings_.size() != 1) {
      settings_[index] = settings_.back();
    }
    settings_.pop_back();
  }
}

const HTTPSetting* HTTPSettings::getSetting(SettingsId id) const {
  auto iter = getSettingConstIter(id);
  if (iter != std::end(settings_)) {
    return &(*iter);
  } else {
    return nullptr;
  }
}

uint32_t HTTPSettings::getSetting(SettingsId id, uint32_t defaultValue) const {
  auto iter = getSettingConstIter(id);
  if (iter != std::end(settings_)) {
    return (*iter).value;
  } else {
    return defaultValue;
  }
}

std::vector<HTTPSetting>::iterator HTTPSettings::getSettingIter(
    SettingsId id) {
  return std::find_if(
    std::begin(settings_), std::end(settings_),
    [&] (HTTPSetting const& s) { return s.id == id; } );
}

std::vector<HTTPSetting>::const_iterator HTTPSettings::getSettingConstIter(
    SettingsId id) const {
  return std::find_if(
    std::begin(settings_), std::end(settings_),
    [&] (HTTPSetting const& s) { return s.id == id; } );
}

}
