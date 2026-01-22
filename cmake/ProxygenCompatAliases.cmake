# Copyright (c) Meta Platforms, Inc. and affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

# Helper macro to create a compat alias with multiple dependencies
macro(proxygen_compat_alias _name)
  add_library(${_name} INTERFACE)
  target_link_libraries(${_name} INTERFACE ${ARGN})
  install(TARGETS ${_name} EXPORT proxygen-exports)
  add_library(proxygen::${_name} ALIAS ${_name})
endmacro()

# =============================================================================
# Backwards compatibility aliases
# =============================================================================

# quicwebtransport: used by moxygen for QUIC WebTransport support
proxygen_compat_alias(quicwebtransport
  proxygen_http_webtransport_quicwebtransport
)

# proxygenhttpserver: namespace alias for the manually maintained target
if(TARGET proxygenhttpserver)
  add_library(proxygen::proxygenhttpserver ALIAS proxygenhttpserver)
endif()

# proxygenhqserver: namespace alias for the manually maintained target (BUILD_SAMPLES)
if(TARGET proxygenhqserver)
  add_library(proxygen::proxygenhqserver ALIAS proxygenhqserver)
endif()

# proxygenhqloggerhelper: namespace alias for the manually maintained target (BUILD_SAMPLES)
if(TARGET proxygenhqloggerhelper)
  add_library(proxygen::proxygenhqloggerhelper ALIAS proxygenhqloggerhelper)
endif()

# proxygendeviousbaton: namespace alias for the manually maintained target (BUILD_SAMPLES)
if(TARGET proxygendeviousbaton)
  add_library(proxygen::proxygendeviousbaton ALIAS proxygendeviousbaton)
endif()
