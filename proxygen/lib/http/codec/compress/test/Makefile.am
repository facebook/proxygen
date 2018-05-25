SUBDIRS = .

check_LTLIBRARIES = libhpacktestutils.la
libhpacktestutils_la_SOURCES = TestUtil.cpp

libhpacktestutilsdir = $(includedir)/proxygen/lib/http/codec/compress/test
nobase_libhpacktestutils_HEADERS = TestUtil.h

check_PROGRAMS = HPACKTests
HPACKTests_SOURCES = \
        HPACKBufferTests.cpp \
        HPACKCodecTests.cpp \
        HPACKContextTests.cpp \
        HPACKHeaderTests.cpp \
        HeaderPieceTests.cpp \
        HeaderTableTests.cpp \
        HuffmanTests.cpp \
        LoggingTests.cpp \
        QPACKCodecTests.cpp \
        QPACKContextTests.cpp \
        QPACKHeaderTableTests.cpp \
        RFCExamplesTests.cpp

HPACKTests_LDADD = \
	../../../libproxygenhttp.la \
	../../../../utils/libutils.la \
	libhpacktestutils.la \
	../../../../test/libtestmain.la

TESTS = HPACKTests
