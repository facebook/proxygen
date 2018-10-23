SUBDIRS = . codec session test

BUILT_SOURCES = HTTPCommonHeaders.h HTTPCommonHeaders.cpp

HTTPCommonHeaders.h: HTTPCommonHeaders.template.h HTTPCommonHeaders.txt
	FBCODE_DIR=$(top_srcdir)/.. INSTALL_DIR=$(srcdir) HEADERS_LIST=$(srcdir)/HTTPCommonHeaders.txt ./gen_HTTPCommonHeaders.h.sh

HTTPCommonHeaders.cpp: HTTPCommonHeaders.template.gperf HTTPCommonHeaders.txt
	FBCODE_DIR=$(top_srcdir)/.. INSTALL_DIR=$(srcdir) HEADERS_LIST=$(srcdir)/HTTPCommonHeaders.txt ./gen_HTTPCommonHeaders.cpp.sh

noinst_LTLIBRARIES = libproxygenhttp.la

libproxygenhttpdir = $(includedir)/proxygen/lib/http
nobase_libproxygenhttp_HEADERS = \
	HTTPCommonHeaders.h \
	HTTPConnector.h \
	HTTPConstants.h \
	HTTPException.h \
	HTTPHeaderSize.h \
	HTTPHeaders.h \
	HTTPMessage.h \
	HTTPMessageFilters.h \
	HTTPMethod.h \
	ProxygenErrorEnum.h \
	experimental/RFC1867.h \
	RFC2616.h \
	Window.h \
	codec/CodecDictionaries.h \
	codec/CodecProtocol.h \
	codec/DefaultHTTPCodecFactory.h \
	codec/ErrorCode.h \
	codec/FlowControlFilter.h \
	codec/HTTP1xCodec.h \
	codec/HTTPChecks.h \
	codec/HTTPCodec.h \
	codec/HTTPCodecFactory.h \
	codec/HTTPCodecFilter.h \
	codec/HTTPCodecPrinter.h \
	codec/HTTPSettings.h \
	codec/SPDYCodec.h \
	codec/HTTPParallelCodec.h \
	codec/SPDYConstants.h \
	codec/CodecUtil.h \
	codec/SPDYVersion.h \
	codec/SPDYVersionSettings.h \
	codec/SettingsId.h \
	codec/TransportDirection.h \
	codec/compress/GzipHeaderCodec.h \
	codec/compress/HeaderIndexingStrategy.h \
	codec/compress/HPACKCodec.h \
	codec/compress/HPACKConstants.h \
	codec/compress/HPACKContext.h \
	codec/compress/HPACKDecodeBuffer.h \
	codec/compress/HPACKDecoder.h \
	codec/compress/HPACKDecoderBase.h \
	codec/compress/HPACKEncodeBuffer.h \
	codec/compress/HPACKEncoder.h \
	codec/compress/HPACKEncoderBase.h \
	codec/compress/HPACKHeader.h \
	codec/compress/HPACKHeaderName.h \
        codec/compress/HPACKStreamingCallback.h \
        codec/compress/HPACKTableInfo.h \
	codec/compress/Header.h \
	codec/compress/HeaderCodec.h \
	codec/compress/HeaderPiece.h \
	codec/compress/HeaderTable.h \
	codec/compress/Huffman.h \
	codec/compress/Logging.h \
	codec/compress/NoPathIndexingStrategy.h \
	codec/compress/QPACKCodec.h \
	codec/compress/QPACKContext.h \
	codec/compress/QPACKDecoder.h \
	codec/compress/QPACKEncoder.h \
	codec/compress/QPACKHeaderTable.h \
	codec/compress/StaticHeaderTable.h \
	codec/HeaderConstants.h \
	codec/HeaderDecodeInfo.h \
	codec/HTTPRequestVerifier.h \
	codec/HTTP2Codec.h \
	codec/HTTP2Constants.h \
	codec/HTTP2Framer.h \
	session/AckLatencyEvent.h \
	session/ByteEventTracker.h \
	session/ByteEvents.h \
	session/CodecErrorResponseHandler.h \
	session/HTTPDefaultSessionCodecFactory.h \
	session/HTTPDirectResponseHandler.h \
	session/HTTPDownstreamSession.h \
	session/HTTPErrorPage.h \
	session/HTTPEvent.h \
	session/HTTPSession.h \
	session/HTTPSessionAcceptor.h \
	session/HTTPSessionBase.h \
	session/HTTPSessionController.h \
	session/HTTPSessionStats.h \
	session/HTTPTransaction.h \
	session/HTTPTransactionEgressSM.h \
	session/HTTPTransactionIngressSM.h \
	session/HTTPUpstreamSession.h \
	session/HTTP2PriorityQueue.h \
	session/SecondaryAuthManager.h \
	session/SecondaryAuthManagerBase.h \
	session/SimpleController.h \
	session/TTLBAStats.h \
	session/TransportFilter.h

libproxygenhttp_la_SOURCES = \
	HTTPCommonHeaders.cpp \
	codec/CodecProtocol.cpp \
	codec/DefaultHTTPCodecFactory.cpp \
	codec/compress/GzipHeaderCodec.cpp \
	codec/compress/HeaderIndexingStrategy.cpp \
	codec/compress/HeaderTable.cpp \
	codec/compress/HPACKCodec.cpp \
	codec/compress/HPACKContext.cpp \
	codec/compress/HPACKDecodeBuffer.cpp \
	codec/compress/HPACKDecoder.cpp \
	codec/compress/HPACKDecoderBase.cpp \
	codec/compress/HPACKEncodeBuffer.cpp \
	codec/compress/HPACKEncoder.cpp \
	codec/compress/HPACKEncoderBase.cpp \
	codec/compress/HPACKHeader.cpp \
	codec/compress/Huffman.cpp \
	codec/compress/NoPathIndexingStrategy.cpp \
	codec/compress/Logging.cpp \
	codec/compress/QPACKCodec.cpp \
	codec/compress/QPACKContext.cpp \
	codec/compress/QPACKDecoder.cpp \
	codec/compress/QPACKEncoder.cpp \
	codec/compress/QPACKHeaderTable.cpp \
	codec/compress/StaticHeaderTable.cpp \
	codec/ErrorCode.cpp \
	codec/HeaderConstants.cpp \
	codec/HeaderDecodeInfo.cpp \
        codec/HTTP2Codec.cpp \
	codec/HTTP2Constants.cpp \
	codec/HTTP2Framer.cpp \
	codec/FlowControlFilter.cpp \
	codec/HTTP1xCodec.cpp \
	codec/HTTPChecks.cpp \
	codec/HTTPCodecFactory.cpp \
	codec/HTTPCodecFilter.cpp \
	codec/HTTPCodecPrinter.cpp \
	codec/HTTPSettings.cpp \
	codec/SPDYCodec.cpp \
	codec/HTTPParallelCodec.cpp \
	codec/SPDYConstants.cpp \
	codec/CodecUtil.cpp \
	codec/TransportDirection.cpp \
	HTTPConnector.cpp \
	HTTPConstants.cpp \
	HTTPException.cpp \
	HTTPHeaders.cpp \
	HTTPMessage.cpp \
	HTTPMethod.cpp \
	ProxygenErrorEnum.cpp \
	experimental/RFC1867.cpp \
	RFC2616.cpp \
	session/ByteEvents.cpp \
	session/CodecErrorResponseHandler.cpp \
	session/HTTPDefaultSessionCodecFactory.cpp \
	session/HTTPDirectResponseHandler.cpp \
	session/HTTPDownstreamSession.cpp \
	session/HTTPErrorPage.cpp \
	session/HTTPEvent.cpp \
	session/HTTPSessionAcceptor.cpp \
	session/HTTPSessionBase.cpp \
	session/HTTPSession.cpp \
	session/HTTPTransaction.cpp \
	session/HTTPTransactionEgressSM.cpp \
	session/HTTPTransactionIngressSM.cpp \
	session/HTTPUpstreamSession.cpp \
	session/HTTP2PriorityQueue.cpp \
	session/ByteEventTracker.cpp \
	session/SecondaryAuthManager.cpp \
	session/SimpleController.cpp \
	session/TransportFilter.cpp \
	Window.cpp

libproxygenhttp_la_LIBADD = \
	../services/libproxygenservices.la \
	../utils/libutils.la

noinst_PROGRAMS = CompressionSimulator

CompressionSimulator_SOURCES = \
	codec/compress/test/HTTPArchive.cpp \
	codec/compress/experimental/simulator/Main.cpp \
	codec/compress/experimental/simulator/CompressionSimulator.cpp \
	codec/compress/experimental/simulator/CompressionUtils.cpp

CompressionSimulatordir = $(includedir)/proxygen/lib/http/codec/compress/experimental/simulator
CompressionSimulator_HEADERS = \
	codec/compress/test/HTTPArchive.h \
	codec/compress/experimental/simulator/CompressionScheme.h \
	codec/compress/experimental/simulator/CompressionSimulator.h \
	codec/compress/experimental/simulator/CompressionTypes.h \
	codec/compress/experimental/simulator/CompressionUtils.h \
	codec/compress/experimental/simulator/HPACKScheme.h \
	codec/compress/experimental/simulator/QPACKScheme.h \
	codec/compress/experimental/simulator/SimStreamingCallback.h

CompressionSimulator_LDADD = \
	libproxygenhttp.la \
	../services/libproxygenservices.la
