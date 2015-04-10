#!/bin/bash

sed "`
  {
    echo -n 's/%%%%%/';
    cat ${HEADERS_LIST?} | sort | uniq \
      | sed 's/-/_/g' \
      | sed 's/.*/  HTTP_HEADER_\U\0 = @@VAL_TOKEN@@,/' \
      | (
        IFS='';
        N=1;
        while read line; do \
          if (echo $line | grep -q '@@VAL_TOKEN@@'); then \
            N=$((++N)) && echo $line | sed "s/@@VAL_TOKEN@@/$N/";
          else \
            echo $line;
          fi;
        done;) \
      | sed 's/$/\\\\n/' \
      | tr -d '\n';
    echo -n '/';
  } \
`" "${FBCODE_DIR?}/proxygen/lib/http/HTTPCommonHeaders.template.h" > "${INSTALL_DIR?}/HTTPCommonHeaders.h"
