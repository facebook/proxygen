#!/bin/bash

sed "`
  {
    echo -n 's/%%%%%/%%\n';
    cat ${HEADERS_LIST?} | sort | uniq \
      | sed 's/.*/\0, HTTP_HEADER_\U\0/' \
      | sed -e ':loop' -e 's/ \([^-]*\)-/ \1_/' -e 't loop' \
      | sed 's/$/\\\\n/' \
      | tr -d '\n';
    echo -n '%%/';
  } \
`" "${FBCODE_DIR?}/proxygen/lib/http/HTTPCommonHeaders.template.gperf" \
| ${GPERF:-gperf} -m5 --output-file="${INSTALL_DIR?}/HTTPCommonHeaders.cpp"
