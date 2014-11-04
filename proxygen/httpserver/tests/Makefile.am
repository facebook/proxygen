SUBDIRS = .

check_PROGRAMS = HTTPServerTests
HTTPServerTests_SOURCES = \
	HTTPServerTest.cpp

HTTPServerTests_LDADD = \
	../libproxygenhttpserver.la \
	../../lib/test/libtestmain.la

TESTS = HTTPServerTests
