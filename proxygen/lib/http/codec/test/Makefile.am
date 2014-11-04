SUBDIRS = .

check_LTLIBRARIES = libcodectestutils.la
libcodectestutils_la_SOURCES = TestUtils.cpp

libcodectestutilsdir = $(includedir)/proxygen/lib/http/codec/test
nobase_libcodectestutils_HEADERS = TestUtils.h

check_PROGRAMS = CodecTests
CodecTests_SOURCES = \
	FilterTests.cpp \
	SPDYCodecTest.cpp \
	HTTP1xCodecTest.cpp

CodecTests_LDADD = \
	../../libproxygenhttp.la \
	../../../utils/libutils.la \
	libcodectestutils.la \
	../../../test/libtestmain.la

TESTS = CodecTests
