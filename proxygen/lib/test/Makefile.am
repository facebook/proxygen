SUBDIRS = .

GTEST_ALL = googletest/googletest/src/gtest-all.cc
BUILT_SOURCES = $(GTEST_ALL)

$(GTEST_ALL):
	test -f googletest/.git || \
		rm -rf googletest && \
		git clone https://github.com/google/googletest.git
	(cd googletest && \
		(git fetch origin || echo "Fetch failed") && \
		git checkout release-1.8.0 -- && \
		cmake . && \
		make \
		)

check_LTLIBRARIES = libtesttransport.la
libtesttransport_la_SOURCES = TestAsyncTransport.cpp

libtesttransportdir = $(includedir)/proxygen/lib/test
nobase_libtesttransport_HEADERS = TestAsyncTransport.h

# libgmockgtest.la is gmock + gtest

check_LTLIBRARIES += libgmockgtest.la
libgmockgtest_la_CPPFLAGS = -Igoogletest/googletest -Igoogletest/googlemock $(AM_CPPFLAGS)
libgmockgtest_la_LDFLAGS = -lglog

libgmockgtest_la_SOURCES = \
	googletest/googletest/src/gtest-all.cc \
	googletest/googlemock/src/gmock-all.cc

check_LTLIBRARIES += libtestmain.la
libtestmain_la_SOURCES = TestMain.cpp
libtestmain_la_LIBADD = libgmockgtest.la
