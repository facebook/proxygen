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
if [ "x$4" != "x" ];then
	export GPERF="$4"
fi

# Some fun stuff going on here.
#
# 1) The `cat` here isn't useless, despite what it may seem. Also, the
# HEADERS_LIST variable should not be quoted. These two are related: although
# HEADERS_LIST in the external version of the source code only ever contains one
# file, the Facebook internal build sets HEADERS_LIST to contain two files. We
# rely on the shell expansion with the space in the variable to split here, so
# the `cat` actually concatenates two files and sends them through to `awk`.
#
# 2) The `awk` script isn't nearly as hairy as it seems. The only real trick is
# the first line. We're processing two files -- the result of the `cat` pipeline
# above, plus the gperf template. The "NR == FNR" compares the current file's
# line number with the total number of lines we've processed -- i.e., that test
# means "am I in the first file?" So we suck those lines aside. Then we process
# the second file, replacing "%%%%%" with some munging of the lines we sucked
# aside from the `cat` pipeline. It is written in awk since it isn't really
# worth the build system BS of calling out to Python (which is unfortunately
# particularly annoying in Facebook's internal build system) and more portable
# (and less hairy honestly) than the massive `sed` pipeline which used to be
# here. And it really isn't that bad, this is the sort of thing `awk` was
# designed for.
cat ${HEADERS_LIST?} | LC_ALL=C sort | uniq \
| awk '
  NR == FNR {
    n[FNR] = $1;
    next
  }
  $1 == "%%%%%" {
    print "%%";
    for (i in n) {
      h = n[i];
      gsub("-", "_", h);
      print n[i] ", HTTP_HEADER_" toupper(h)
    };
    print "%%";
    next
  }
  {
    print
  }
' - "${FBCODE_DIR?}/proxygen/lib/http/HTTPCommonHeaders.template.gperf" \
| ${GPERF:-gperf} -m5 --output-file="${INSTALL_DIR?}/HTTPCommonHeaders.cpp"
if [[ "$(readlink -f test 2>/dev/null)" ]]; then
    sed -i  "s:$(readlink -f ${FBCODE_DIR?})/::g" "${INSTALL_DIR?}/HTTPCommonHeaders.cpp"
fi
