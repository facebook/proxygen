SUBDIRS = .

noinst_PROGRAMS = proxy_server

proxy_server_SOURCES = \
	ProxyHandler.cpp \
	ProxyServer.cpp

proxy_server_LDADD = \
	../../libproxygenhttpserver.la \
        ../../../lib/http/libproxygenhttp.la
