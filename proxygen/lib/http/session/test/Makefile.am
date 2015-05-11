SUBDIRS = .

check_PROGRAMS = SessionTests
SessionTests_SOURCES = \
	HTTPTransactionSMTest.cpp \
	DownstreamTransactionTest.cpp \
	HTTPDownstreamSessionTest.cpp \
	HTTPSessionAcceptorTest.cpp \
	HTTPUpstreamSessionTest.cpp \
	HTTP2PriorityQueueTest.cpp \
	MockCodecDownstreamTest.cpp \
	TestUtils.cpp

SessionTests_LDADD = \
	../../../services/libproxygenservices.la \
	../../../test/libtestmain.la \
	../../../test/libtesttransport.la \
	../../../utils/libutils.la \
	../../codec/test/libcodectestutils.la \
	../../libproxygenhttp.la

TESTS = SessionTests
