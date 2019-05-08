# Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved
SUBDIRS = .

check_PROGRAMS = SessionPoolTest
SessionPoolTest_SOURCES = SessionPoolTest.cpp

SessionPoolTest_LDADD = \
	../../libproxygenhttp.la \
	../../../services/libproxygenservices.la \
	../../../test/libtesttransport.la \
	../../../test/libgmockgtest.la \
	../../../utils/libutils.la

TESTS = SessionPoolTest
