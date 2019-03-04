# Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved
SUBDIRS = .

check_PROGRAMS = HTTPServerFilterTests
HTTPServerTests_SOURCES = \
	ZlibServerFilterTest.cpp

HTTPServerTests_LDADD = \
	../libproxygenhttpserver.la \
	../../lib/test/libtestmain.la

TESTS = HTTPServerFilterTests
