SUBDIRS = .

check_PROGRAMS = UtilTests
UtilTests_SOURCES = \
	AsyncTimeoutSetTest.cpp \
	GenericFilterTest.cpp \
	HTTPTimeTest.cpp \
	ParseURLTest.cpp \
	ResultTest.cpp \
	UtilTest.cpp

UtilTests_LDADD = ../libutils.la ../../test/libtestmain.la

TESTS = UtilTests
