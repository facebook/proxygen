/* Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "http_parser.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace proxygen {

// Helper function to parse a URL
static int parse_url(const char* buf,
                     size_t buflen,
                     int is_connect,
                     struct http_parser_url* u,
                     uint8_t options) {
  // Initialize the URL structure
  memset(u, 0, sizeof(*u));

  // If it's a CONNECT request, the URL is just host:port
  if (is_connect) {
    // Find the colon separating host and port
    const char* colon = static_cast<const char*>(memchr(buf, ':', buflen));
    if (colon == nullptr) {
      return 1; // No port found
    }

    // Set the host field
    u->field_set |= (1 << UF_HOST);
    u->field_data[UF_HOST].off = 0;
    u->field_data[UF_HOST].len = colon - buf;

    // Set the port field
    u->field_set |= (1 << UF_PORT);
    u->field_data[UF_PORT].off = colon - buf + 1;
    u->field_data[UF_PORT].len = buflen - (colon - buf) - 1;

    // Parse the port number
    const char* port_str = buf + u->field_data[UF_PORT].off;
    size_t port_len = u->field_data[UF_PORT].len;

    // Validate port is numeric
    for (size_t i = 0; i < port_len; i++) {
      if (!isdigit(port_str[i])) {
        return 1; // Invalid port
      }
    }

    // Convert port to integer
    char port_buf[6]; // Max port is 65535 (5 digits)
    if (port_len >= sizeof(port_buf)) {
      return 1; // Port too long
    }
    memcpy(port_buf, port_str, port_len);
    port_buf[port_len] = '\0';
    u->port = static_cast<uint16_t>(atoi(port_buf));

    return 0;
  }

  // Parse a regular URL
  // Format: [scheme://][userinfo@]host[:port][/path][?query][#fragment]

  const char* p = buf;
  const char* end = buf + buflen;

  // Parse scheme
  const char* scheme_start = p;
  const char* scheme_end = nullptr;
  for (; p < end; p++) {
    if (*p == ':') {
      scheme_end = p;
      p++; // Skip ':'
      break;
    } else if (!isalpha(*p) && *p != '+' && *p != '-' && *p != '.') {
      // Invalid scheme character
      break;
    }
  }

  // If we found a valid scheme
  if (scheme_end && p + 1 < end && p[0] == '/' && p[1] == '/') {
    u->field_set |= (1 << UF_SCHEMA);
    u->field_data[UF_SCHEMA].off = scheme_start - buf;
    u->field_data[UF_SCHEMA].len = scheme_end - scheme_start;
    p += 2; // Skip "//"
  } else {
    // No scheme or invalid scheme, reset to beginning
    p = buf;
  }

  // Parse userinfo if present
  const char* userinfo_start = p;
  const char* userinfo_end = nullptr;
  for (; p < end; p++) {
    if (*p == '@') {
      userinfo_end = p;
      p++; // Skip '@'
      break;
    } else if (*p == '/' || *p == '?' || *p == '#') {
      // No userinfo
      break;
    }
  }

  if (userinfo_end) {
    u->field_set |= (1 << UF_USERINFO);
    u->field_data[UF_USERINFO].off = userinfo_start - buf;
    u->field_data[UF_USERINFO].len = userinfo_end - userinfo_start;
  } else {
    // No userinfo, reset to after scheme
    p = userinfo_start;
  }

  // Parse host
  const char* host_start = p;
  const char* host_end = nullptr;

  // Check for IPv6 literal
  if (p < end && *p == '[') {
    p++;
    for (; p < end; p++) {
      if (*p == ']') {
        host_end = p + 1; // Include the closing bracket
        p++;
        break;
      }
    }
    if (!host_end) {
      return 1; // Unclosed IPv6 literal
    }
  } else {
    // Regular hostname or IPv4
    for (; p < end; p++) {
      if (*p == ':' || *p == '/' || *p == '?' || *p == '#') {
        host_end = p;
        break;
      }
    }
    if (!host_end && p == end) {
      host_end = p;
    }
  }

  if (host_end) {
    u->field_set |= (1 << UF_HOST);
    u->field_data[UF_HOST].off = host_start - buf;
    u->field_data[UF_HOST].len = host_end - host_start;
  } else {
    return 1; // No valid host
  }

  // Parse port if present
  if (p < end && *p == ':') {
    p++; // Skip ':'
    const char* port_start = p;
    const char* port_end = nullptr;
    for (; p < end; p++) {
      if (*p == '/' || *p == '?' || *p == '#') {
        port_end = p;
        break;
      }
    }
    if (!port_end && p == end) {
      port_end = p;
    }

    if (port_end && port_end > port_start) {
      u->field_set |= (1 << UF_PORT);
      u->field_data[UF_PORT].off = port_start - buf;
      u->field_data[UF_PORT].len = port_end - port_start;

      // Convert port to integer
      size_t port_len = port_end - port_start;
      char port_buf[6]; // Max port is 65535 (5 digits)
      if (port_len >= sizeof(port_buf)) {
        return 1; // Port too long
      }

      // Validate port is numeric
      for (size_t i = 0; i < port_len; i++) {
        if (!isdigit(port_start[i])) {
          return 1; // Invalid port
        }
      }

      memcpy(port_buf, port_start, port_len);
      port_buf[port_len] = '\0';
      u->port = static_cast<uint16_t>(atoi(port_buf));
    }
  }

  // Parse path if present
  if (p < end && *p == '/') {
    const char* path_start = p;
    const char* path_end = nullptr;
    for (; p < end; p++) {
      if (*p == '?' || *p == '#') {
        path_end = p;
        break;
      }
    }
    if (!path_end && p == end) {
      path_end = p;
    }

    if (path_end) {
      u->field_set |= (1 << UF_PATH);
      u->field_data[UF_PATH].off = path_start - buf;
      u->field_data[UF_PATH].len = path_end - path_start;
    }
  } else if (!(u->field_set & (1 << UF_PATH)) && p < end) {
    // If no path was found but we're not at the end, set an empty path
    u->field_set |= (1 << UF_PATH);
    u->field_data[UF_PATH].off = p - buf;
    u->field_data[UF_PATH].len = 0;
  }

  // Parse query if present
  if (p < end && *p == '?') {
    p++; // Skip '?'
    const char* query_start = p;
    const char* query_end = nullptr;
    for (; p < end; p++) {
      if (*p == '#') {
        query_end = p;
        break;
      }
    }
    if (!query_end && p == end) {
      query_end = p;
    }

    if (query_end) {
      u->field_set |= (1 << UF_QUERY);
      u->field_data[UF_QUERY].off = query_start - buf;
      u->field_data[UF_QUERY].len = query_end - query_start;
    }
  }

  // Parse fragment if present
  if (p < end && *p == '#') {
    p++; // Skip '#'
    const char* fragment_start = p;
    const char* fragment_end = end;

    u->field_set |= (1 << UF_FRAGMENT);
    u->field_data[UF_FRAGMENT].off = fragment_start - buf;
    u->field_data[UF_FRAGMENT].len = fragment_end - fragment_start;
  }

  // Apply strict validation if requested
  if (options & F_PARSE_URL_OPTIONS_URL_STRICT) {
    // In strict mode, ensure we have at least a host or a path
    if (!(u->field_set & ((1 << UF_HOST) | (1 << UF_PATH)))) {
      return 1;
    }

    // In strict mode, ensure the path starts with / if we have a scheme
    if ((u->field_set & (1 << UF_SCHEMA)) && (u->field_set & (1 << UF_PATH)) &&
        u->field_data[UF_PATH].len > 0) {
      if (buf[u->field_data[UF_PATH].off] != '/') {
        return 1;
      }
    }
  }

  return 0;
}

int http_parser_parse_url(const char* buf,
                          size_t buflen,
                          int is_connect,
                          struct http_parser_url* u) {
  return parse_url(buf, buflen, is_connect, u, 0);
}

int http_parser_parse_url_options(const char* buf,
                                  size_t buflen,
                                  int is_connect,
                                  struct http_parser_url* u,
                                  uint8_t options) {
  return parse_url(buf, buflen, is_connect, u, options);
}

} // namespace proxygen
