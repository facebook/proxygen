SUBDIRS = .

check_PROGRAMS = HTTPServerTests
HTTPServerTests_SOURCES = \
	HTTPServerTest.cpp

HTTPServerTests_LDADD = \
	../libproxygenhttpserver.la \
	../../lib/test/libtestmain.la \
	../../httpclient/samples/curl/libproxygencurl.la

TESTS = HTTPServerTests
