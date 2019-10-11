/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <proxygen/lib/http/codec/HQFramer.h>
#include <quic/codec/Types.h>

#include <boost/bimap.hpp>
#include <boost/bimap/list_of.hpp>
#include <boost/bimap/unordered_set_of.hpp>

namespace proxygen {
// Lookup tags
struct push_id {};
struct quic_stream_id {};

using PushToStreamMap = boost::bimap<
    boost::bimaps::unordered_set_of<boost::bimaps::tagged<hq::PushId, push_id>>,
    boost::bimaps::unordered_set_of<
        boost::bimaps::tagged<quic::StreamId, quic_stream_id>>,
    boost::bimaps::list_of_relation>;
}; // namespace proxygen
