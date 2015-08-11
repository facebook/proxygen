#!/bin/bash

if [ "x$1" != "x" ];then
	export HEADERS_LIST="$1"
fi
if [ "x$2" != "x" ];then
	export FBCODE_DIR="$2"
fi
if [ "x$3" != "x" ];then
	export INSTALL_DIR="$3"
fi

# gen_HTTPCommonHeaders.cpp.sh contains a substantially similar pipeline and
# awk script -- see comments there.
cat ${HEADERS_LIST?} | sort | uniq \
| awk '
  NR == FNR {
    n[FNR] = $1;
    next
  }
  $1 == "%%%%%" {
    for (i in n) {
      h = n[i];
      gsub("-", "_", h);
      print "  HTTP_HEADER_" toupper(h) " = " i+1 ","
    };
    next
  }
  {
    print
  }
' - "${FBCODE_DIR?}/proxygen/lib/http/HTTPCommonHeaders.template.h" > "${INSTALL_DIR?}/HTTPCommonHeaders.h"
