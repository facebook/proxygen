/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

// From https://gcc.gnu.org/wiki/Visibility
#if defined _WIN32 || defined __CYGWIN__
  #ifdef BUILDING_DLL
    #ifdef __GNUC__
      #define FB_EXPORT __attribute__ ((dllexport))
    #else
      #define FB_EXPORT __declspec(dllexport)
    #endif
  #else
    #ifdef __GNUC__
      #define FB_EXPORT __attribute__ ((dllimport))
    #else
      #define FB_EXPORT __declspec(dllimport)
    #endif
  #endif
  #define FB_LOCAL
#else
  #if __GNUC__ >= 4
    #define FB_EXPORT __attribute__ ((visibility ("default")))
    #define FB_LOCAL  __attribute__ ((visibility ("hidden")))
  #else
    #define FB_EXPORT
    #define FB_LOCAL
  #endif
#endif
