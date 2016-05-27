SUBDIRS = .

lib_LTLIBRARIES = libproxygencurl.la

libproxygencurldir = $(includedir)/proxygen/httpclient/samples/curl
nobase_libproxygencurl_HEADERS = \
	CurlClient.h

libproxygencurl_la_SOURCES = \
	CurlClient.cpp

libproxygencurl_la_LIBADD = \
        ../../../lib/libproxygenlib.la \
	../../../lib/http/libproxygenhttp.la

libproxygencurl_la_LDFLAGS=-static

noinst_PROGRAMS = curl

curl_SOURCES = \
        CurlClientMain.cpp

curl_LDADD = \
	libproxygencurl.la

