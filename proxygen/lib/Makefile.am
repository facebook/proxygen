SUBDIRS = test utils ssl services http

lib_LTLIBRARIES = libproxygenlib.la

libproxygenlib_la_SOURCES =
nodist_EXTRA_libproxygenlib_la_SOURCES = dummy.cpp
libproxygenlib_la_LIBADD = \
	http/libproxygenhttp.la \
	services/libproxygenservices.la \
	ssl/libproxygenssl.la \
	utils/libutils.la

libproxygenlib_la_LDFLAGS=-static
