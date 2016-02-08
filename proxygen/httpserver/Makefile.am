SUBDIRS = . samples tests

lib_LTLIBRARIES = libproxygenhttpserver.la

libproxygenhttpserverdir = $(includedir)/proxygen/httpserver
nobase_libproxygenhttpserver_HEADERS = \
	filters/DirectResponseHandler.h \
	filters/RejectConnectFilter.h \
	filters/ZlibServerFilter.h \
	Filters.h \
	HTTPServer.h \
	HTTPServerAcceptor.h \
	HTTPServerOptions.h \
	Mocks.h \
	PushHandler.h \
	RequestHandler.h \
	RequestHandlerAdaptor.h \
	RequestHandlerFactory.h \
	ResponseBuilder.h \
	ResponseHandler.h \
	ScopedHTTPServer.h \
	SignalHandler.h

libproxygenhttpserver_la_SOURCES = \
	HTTPServer.cpp \
	HTTPServerAcceptor.cpp \
	RequestHandlerAdaptor.cpp \
	SignalHandler.cpp

libproxygenhttpserver_la_LIBADD = \
	../lib/libproxygenlib.la
