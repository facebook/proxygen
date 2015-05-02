#!/bin/bash
if [[ "$OSTYPE" == "darwin"* ]]; then
  SED=${SED:-gsed}
else
  SED=${SED:-sed}
fi

${SED} "`
  {
    echo -n 's/%%%%%/%%\n';
    cat ${HEADERS_LIST?} | sort | uniq \
      | ${SED} 's/.*/\0, HTTP_HEADER_\U\0/' \
      | ${SED} -e ':loop' -e 's/ \([^-]*\)-/ \1_/' -e 't loop' \
      | ${SED} 's/$/\\\\n/' \
      | tr -d '\n';
    echo -n '%%/';
  } \
`" "${FBCODE_DIR?}/proxygen/lib/http/HTTPCommonHeaders.template.gperf" \
| ${GPERF:-gperf} -m5 --output-file="${INSTALL_DIR?}/HTTPCommonHeaders.cpp"
