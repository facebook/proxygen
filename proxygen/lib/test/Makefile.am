SUBDIRS = .

BUILT_SOURCES = googletest-release-1.8.0/googletest/src/gtest-all.cc

release-1.8.0.zip:
	wget https://github.com/google/googletest/archive/release-1.8.0.zip

# The SHA1 test is in a separate rule from the fetch, because otherwise
# `make` would run `sha1sum` **before** wget.  We'd re-extract the archive
# without `touch`, since the contents' timestamps are older than the zip's.
googletest-release-1.8.0/googletest/src/gtest-all.cc: release-1.8.0.zip
	[ "$(shell sha1sum release-1.8.0.zip | awk '{print $$1}')" == \
	    "667f873ab7a4d246062565fad32fb6d8e203ee73" ]
	unzip release-1.8.0.zip
	touch googletest-release-1.8.0/googletest/src/gtest-all.cc


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
