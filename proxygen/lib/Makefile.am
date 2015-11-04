SUBDIRS = test utils ssl services http transport

lib_LTLIBRARIES = libproxygenlib.la

libproxygenlib_la_SOURCES =
nodist_EXTRA_libproxygenlib_la_SOURCES = dummy.cpp
libproxygenlib_la_LIBADD = \
	http/libproxygenhttp.la \
	services/libproxygenservices.la \
	ssl/libproxygenssl.la \
	transport/libproxygentransport.la \
	utils/libutils.la

libproxygenlib_la_LDFLAGS=-static
