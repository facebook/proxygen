SUBDIRS = .

noinst_LTLIBRARIES = libproxygenservices.la

libproxygenservicesdir = $(includedir)/proxygen/lib/services
nobase_libproxygenservices_HEADERS = \
	Acceptor.h \
	AcceptorConfiguration.h \
	ConnectionCounter.h \
	HTTPAcceptor.h \
	LoadShedConfiguration.h \
	NetworkAddress.h \
	RequestWorker.h \
	ServerSocketConfig.h \
	Service.h \
	ServiceConfiguration.h \
	ServiceWorker.h \
	TransportInfo.h \
	WorkerThread.h

libproxygenservices_la_SOURCES = \
	Acceptor.cpp \
	LoadShedConfiguration.cpp \
	RequestWorker.cpp \
	Service.cpp \
	TransportInfo.cpp \
	WorkerThread.cpp

libproxygenservices_la_LIBADD = ../ssl/libproxygenssl.la
