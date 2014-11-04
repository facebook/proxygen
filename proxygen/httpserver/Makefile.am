SUBDIRS = . samples tests

lib_LTLIBRARIES = libproxygenhttpserver.la

libproxygenhttpserverdir = $(includedir)/proxygen/httpserver
nobase_libproxygenhttpserver_HEADERS = \
	Filters.h \
	HTTPServer.h \
	HTTPServerAcceptor.h \
	HTTPServerOptions.h \
	Mocks.h \
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
