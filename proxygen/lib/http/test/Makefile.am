SUBDIRS = .

check_PROGRAMS = LibHTTPTests
LibHTTPTests_SOURCES = \
	HTTPMessageTest.cpp \
	RFC2616Test.cpp \
	WindowTest.cpp

LibHTTPTests_LDADD = \
	../libproxygenhttp.la \
	../../utils/libutils.la \
	../../test/libtestmain.la

TESTS = LibHTTPTests
