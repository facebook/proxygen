# Copyright (c) Meta Platforms, Inc. and affiliates.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.

# Manually maintained - granular targets for HQ server libraries
# These break the dependency on monolithic proxygen for downstream projects (moxygen)

proxygen_add_library(proxygen_httpserver_samples_hq_hq_logger_helper
  SRCS
    HQLoggerHelper.cpp
  EXPORTED_DEPS
    mvfst::mvfst_logging_file_qlogger
    mvfst::mvfst_logging_qlogger
)

proxygen_add_library(proxygen_httpserver_samples_hq_hq_params
  SRCS
    HQParams.cpp
  EXPORTED_DEPS
    proxygen_http_http_headers
    proxygen_http_session_hq_session
    mvfst::mvfst_congestion_control_congestion_controller_factory
    mvfst::mvfst_constants
    mvfst::mvfst_fizz_client_handshake_psk_cache
    mvfst::mvfst_state_transport_settings
    fizz::fizz_server_fizz_server_context
    Folly::folly_network_address
    Folly::folly_optional
)

proxygen_add_library(proxygen_httpserver_samples_hq_fizz_context
  SRCS
    FizzContext.cpp
  DEPS
    fizz::fizz_backend_openssl
    fizz::fizz_compression_zlib_certificate_decompressor
    fizz::fizz_compression_zstd_certificate_decompressor
    fizz::fizz_server_aead_ticket_cipher
    fizz::fizz_server_cert_manager
    fizz::fizz_server_ticket_codec
    Folly::folly_file_util
    Folly::folly_random
  EXPORTED_DEPS
    proxygen_httpserver_samples_hq_hq_params
    wangle::wangle_ssl_ssl_context_config
    fizz::fizz_client_fizz_client_context
    fizz::fizz_server_fizz_server_context
)

proxygen_add_library(proxygen_httpserver_samples_hq_HQServer
  SRCS
    HQServer.cpp
  DEPS
    proxygen_http_session_hq_downstream_session
    proxygen_httpserver_samples_hq_hq_logger_helper
    mvfst::mvfst_common_udpsocket_folly_async_udp_socket
  EXPORTED_DEPS
    proxygen_http_codec_http1x_codec
    proxygen_http_session_http_transaction
    proxygen_http_session_server
    proxygen_httpserver_samples_hq_fizz_context
    proxygen_httpserver_samples_hq_hq_params
    mvfst::mvfst_api_stream_async_transport
    mvfst::mvfst_api_transport
    mvfst::mvfst_common_events_folly_eventbase
    mvfst::mvfst_server_quic_handshake_socket_holder
    mvfst::mvfst_server_server
    Folly::folly_io_async_async_base
)

proxygen_add_library(proxygen_httpserver_samples_hq_insecure_verifier
  EXPORTED_DEPS
    fizz::fizz_protocol_certificate_verifier
)

proxygen_add_library(proxygen_httpserver_samples_hq_conn_id_logger
  EXPORTED_DEPS
    Folly::folly_conv
    Folly::folly_file
    Folly::folly_string
    Folly::folly_synchronized
    ${GFLAG_DEPENDENCIES}
    glog::glog
)

proxygen_add_library(proxygen_httpserver_samples_hq_hq_command_line
  SRCS
    HQCommandLine.cpp
  DEPS
    proxygen_http_synchronized_quic_lrucache
    proxygen_httpclient_samples_curl_curl_client_lib
    proxygen_transport_persistent_quic_psk_cache
    mvfst::mvfst_constants
    Folly::folly_io_async_event_base_manager
    Folly::folly_portability_gflags
  EXPORTED_DEPS
    proxygen_httpserver_httpserver_headers
    proxygen_httpserver_samples_hq_hq_params
)
