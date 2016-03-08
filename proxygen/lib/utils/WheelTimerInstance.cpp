/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <proxygen/lib/utils/WheelTimerInstance.h>

#include <proxygen/lib/utils/SharedWheelTimer.h>

#include <folly/Singleton.h>

namespace proxygen {

WheelTimerInstance::WheelTimerInstance() {
}

WheelTimerInstance::WheelTimerInstance(folly::HHWheelTimer* timer) :
  wheelTimerPtr_(timer) {
}

WheelTimerInstance::WheelTimerInstance(
    std::chrono::milliseconds defaultTimeoutMS,
    folly::EventBase* eventBase) : defaultTimeoutMS_(defaultTimeoutMS) {
  auto sharedWheelTimerSingleton =
    folly::Singleton<SharedWheelTimer>::try_get();
  if (sharedWheelTimerSingleton) { // if singleton is destroyed
                                   // then scheduleTimeout will not work
    wheelTimer_ = sharedWheelTimerSingleton->get(eventBase);
  }
}

WheelTimerInstance::WheelTimerInstance(const WheelTimerInstance& timerInstance)
  : wheelTimerPtr_(timerInstance.wheelTimerPtr_),
  wheelTimer_(timerInstance.wheelTimer_),
  defaultTimeoutMS_(timerInstance.defaultTimeoutMS_) {
}

WheelTimerInstance::WheelTimerInstance(
    WheelTimerInstance&& timerInstance) noexcept
  : wheelTimerPtr_(std::move(timerInstance.wheelTimerPtr_)),
    wheelTimer_(std::move(timerInstance.wheelTimer_)),
    defaultTimeoutMS_(std::move(timerInstance.defaultTimeoutMS_)) {
}

std::chrono::milliseconds WheelTimerInstance::getDefaultTimeout() const {
  return defaultTimeoutMS_;
}

void WheelTimerInstance::setDefaultTimeout(std::chrono::milliseconds timeout) {
  defaultTimeoutMS_ = timeout;
}

void WheelTimerInstance::scheduleTimeout(
    folly::HHWheelTimer::Callback* callback,
    std::chrono::milliseconds timeout) {
  if (wheelTimerPtr_ != nullptr) {
    wheelTimerPtr_->scheduleTimeout(callback, timeout);
  } else {
    if (auto wt = wheelTimer_.lock()) { // if pointer does not exist anymore
                                        // it means process is dying and
                                        // there is no point scheduling
                                        // any timeouts
      wt->scheduleTimeout(callback, timeout);
    } else {
      // this is empty WheelTimerInstance case
    }
  }
}

void WheelTimerInstance::scheduleTimeout(
    folly::HHWheelTimer::Callback* callback) {
  scheduleTimeout(callback, wheelTimerPtr_ == nullptr ?
      defaultTimeoutMS_ : wheelTimerPtr_->getDefaultTimeout());
}

WheelTimerInstance& WheelTimerInstance::operator=(const WheelTimerInstance& t) {
  wheelTimerPtr_ = t.wheelTimerPtr_;
  wheelTimer_ = t.wheelTimer_;
  defaultTimeoutMS_ = t.defaultTimeoutMS_;
  return *this;
}

WheelTimerInstance& WheelTimerInstance::
    operator=(const WheelTimerInstance&& timer) {
  wheelTimerPtr_ = std::move(timer.wheelTimerPtr_);
  wheelTimer_ = std::move(timer.wheelTimer_);
  defaultTimeoutMS_ = std::move(timer.defaultTimeoutMS_);
  return *this;
}

WheelTimerInstance::operator bool() const {
  return (wheelTimerPtr_ != nullptr) || (!wheelTimer_.expired());
}

}
