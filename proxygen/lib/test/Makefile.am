SUBDIRS = .

BUILT_SOURCES = gmock-1.7.0/gtest/src/gtest-all.cc

gmock-1.7.0/gtest/src/gtest-all.cc:
	wget https://googlemock.googlecode.com/files/gmock-1.7.0.zip
	unzip gmock-1.7.0.zip


check_LTLIBRARIES = libtesttransport.la
libtesttransport_la_SOURCES = TestAsyncTransport.cpp

libtesttransportdir = $(includedir)/proxygen/lib/test
nobase_libtesttransport_HEADERS = TestAsyncTransport.h

# libgmockgtest.la is gmock + gtest

check_LTLIBRARIES += libgmockgtest.la
libgmockgtest_la_CPPFLAGS = -Igmock-1.7.0/gtest -Igmock-1.7.0 -Igmock-1.7.0/gtest/include -Igmock-1.7.0/include -lglog

libgmockgtest_la_SOURCES = \
	gmock-1.7.0/gtest/src/gtest-all.cc \
	gmock-1.7.0/src/gmock-all.cc

check_LTLIBRARIES += libtestmain.la
libtestmain_la_SOURCES = TestMain.cpp
libtestmain_la_LIBADD = libgmockgtest.la
