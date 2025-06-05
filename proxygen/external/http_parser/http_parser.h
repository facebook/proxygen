/* Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef http_parser_h
#define http_parser_h

/**
 * This is a simple redirect header that includes the llhttp headers directly
 * and provides URL parsing functionality that was in the old http_parser.
 */

#include <proxygen/external/lhttp/api.h>
#include <proxygen/external/lhttp/llhttp.h>
#include <stdint.h>

#ifdef __cplusplus
namespace proxygen {
#endif /* __cplusplus */

/* URL parser field enum values */
enum http_parser_url_fields {
  UF_SCHEMA = 0,
  UF_HOST = 1,
  UF_PORT = 2,
  UF_PATH = 3,
  UF_QUERY = 4,
  UF_FRAGMENT = 5,
  UF_USERINFO = 6,
  UF_MAX = 7
};

/* URL parser result structure */
struct http_parser_url {
  uint16_t field_set; /* Bitmask of (1 << UF_*) values */
  uint16_t port;      /* Converted UF_PORT string */

  struct {
    uint16_t off; /* Offset into buffer in which field starts */
    uint16_t len; /* Length of run in buffer */
  } field_data[UF_MAX];
};

/* Options for http_parser_parse_url */
enum http_parser_parse_url_options {
  F_PARSE_URL_OPTIONS_URL_STRICT = (1 << 0)
};

/* Parse a URL; return nonzero on failure */
int http_parser_parse_url(const char *buf,
                          size_t buflen,
                          int is_connect,
                          struct http_parser_url *u);

/* Parse a URL with options; return nonzero on failure */
int http_parser_parse_url_options(const char *buf,
                                  size_t buflen,
                                  int is_connect,
                                  struct http_parser_url *u,
                                  uint8_t options);

#ifdef __cplusplus
} /* namespace proxygen */
#endif /* __cplusplus */

#endif /* http_parser_h */
