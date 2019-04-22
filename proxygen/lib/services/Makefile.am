# Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved
SUBDIRS = .

noinst_LTLIBRARIES = libproxygenservices.la

libproxygenservicesdir = $(includedir)/proxygen/lib/services
nobase_libproxygenservices_HEADERS = \
	AcceptorConfiguration.h \
	HTTPAcceptor.h \
	RequestWorkerThread.h \
	Service.h \
	ServiceConfiguration.h \
	ServiceWorker.h \
	WorkerThread.h

libproxygenservices_la_SOURCES = \
	RequestWorkerThread.cpp \
	Service.cpp \
	WorkerThread.cpp
