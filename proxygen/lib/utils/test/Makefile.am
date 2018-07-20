SUBDIRS = .

check_PROGRAMS = UtilTests TraceEventTest AsyncTimeoutSetTest

UtilTests_SOURCES = \
	GenericFilterTest.cpp \
	HTTPTimeTest.cpp \
	ParseURLTest.cpp \
	UtilTest.cpp

UtilTests_LDADD = \
	../libutils.la \
	../../test/libtestmain.la

TraceEventTest_SOURCES = \
	TraceEventTest.cpp

TraceEventTest_LDADD = \
	../libutils.la \
	../../test/libtestmain.la

AsyncTimeoutSetTest_SOURCES = \
	AsyncTimeoutSetTest.cpp

AsyncTimeoutSetTest_LDADD = \
	../libutils.la \
	../../test/libtestmain.la

TESTS = UtilTests TraceEventTest AsyncTimeoutSetTest
