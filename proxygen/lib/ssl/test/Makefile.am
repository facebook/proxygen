SUBDIRS = .

check_PROGRAMS = SSLTests
SSLTests_SOURCES = \
	SSLContextManagerTest.cpp

SSLTests_LDADD = \
	../libproxygenssl.la \
	../../utils/libutils.la \
	../../test/libtestmain.la

check_PROGRAMS += SSLCacheTest
SSLCacheTest_SOURCES = \
	SSLCacheTest.cpp

TESTS = SSLTests
