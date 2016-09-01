SUBDIRS = .

BUILT_SOURCES = googletest-release-1.8.0/googletest/src/gtest-all.cc

googletest-release-1.8.0/googletest/src/gtest-all.cc:
	wget https://github.com/google/googletest/archive/release-1.8.0.zip
	unzip release-1.8.0.zip


check_LTLIBRARIES = libtesttransport.la
libtesttransport_la_SOURCES = TestAsyncTransport.cpp

libtesttransportdir = $(includedir)/proxygen/lib/test
nobase_libtesttransport_HEADERS = TestAsyncTransport.h

# libgmockgtest.la is gmock + gtest

check_LTLIBRARIES += libgmockgtest.la
libgmockgtest_la_CPPFLAGS = -Igoogletest-release-1.8.0/googletest -Igoogletest-release-1.8.0/googlemock -Igoogletest-release-1.8.0/googletest/include -Igoogletest-release-1.8.0/googlemock/include -lglog

libgmockgtest_la_SOURCES = \
	googletest-release-1.8.0/googletest/src/gtest-all.cc \
	googletest-release-1.8.0/googlemock/src/gmock-all.cc

check_LTLIBRARIES += libtestmain.la
libtestmain_la_SOURCES = TestMain.cpp
libtestmain_la_LIBADD = libgmockgtest.la
