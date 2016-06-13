SUBDIRS = .

check_PROGRAMS = UtilTests AsyncTimeoutSetTest

UtilTests_SOURCES = \
	DestructorCheckTest.cpp \
	GenericFilterTest.cpp \
	HTTPTimeTest.cpp \
	ParseURLTest.cpp \
	ResultTest.cpp \
	UtilTest.cpp

UtilTests_LDADD = ../libutils.la ../../test/libtestmain.la

AsyncTimeoutSetTest_SOURCES = \
	AsyncTimeoutSetTest.cpp

AsyncTimeoutSetTest_LDADD = \
	../libutils.la \
	../../test/libtestmain.la

TESTS = UtilTests AsyncTimeoutSetTest
