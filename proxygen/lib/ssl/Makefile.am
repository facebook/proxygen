SUBDIRS = . test

noinst_LTLIBRARIES = libproxygenssl.la

libproxygenssldir = $(includedir)/proxygen/lib/ssl
nobase_libproxygenssl_HEADERS = \
	ClientHelloExtStats.h \
	DHParam.h \
	PasswordInFile.h \
	SSLCacheOptions.h \
	SSLCacheProvider.h \
	SSLContextConfig.h \
	SSLContextManager.h \
	SSLSessionCacheManager.h \
	SSLStats.h \
	SSLUtil.h \
	TLSTicketKeyManager.h \
	TLSTicketKeySeeds.h

libproxygenssl_la_SOURCES = \
	PasswordInFile.cpp \
	SSLContextManager.cpp \
	SSLSessionCacheManager.cpp \
	SSLUtil.cpp \
	TLSTicketKeyManager.cpp

libproxygenssl_la_LIBADD = \
	../utils/libutils.la
