SUBDIRS = . test

BUILT_SOURCES = TraceEventType.h TraceEventType.cpp TraceFieldType.h TraceFieldType.cpp

TraceGen: TraceEventType.txt TraceFieldType.txt
	python gen_trace_event_constants.py \
	--output_type=cpp \
	--input_files=TraceEventType.txt,TraceFieldType.txt \
	--output_scope=proxygen \
	--header_path=proxygen/lib/utils \
	--install_dir=$(srcdir) \
	--fbcode_dir=$(srcdir)
	touch TraceGen

TraceEventType.h: TraceGen

TraceEventType.cpp: TraceGen

TraceFieldType.h: TraceGen

TraceFieldType.cpp: TraceGen

noinst_LTLIBRARIES = libutils.la

# We put the generated files first so that we create them first
libutilsdir = $(includedir)/proxygen/lib/utils
nobase_libutils_HEADERS = \
	AsyncTimeoutSet.h \
	Base64.h \
	ChromeUtils.h \
	CobHelper.h \
	CryptUtil.h \
	DestructorCheck.h \
	Exception.h \
	Export.h \
	FilterChain.h \
	HTTPTime.h \
	ParseURL.h \
	Result.h \
	StateMachine.h \
	TestUtils.h \
	Time.h \
	TraceEvent.h \
	TraceEventContext.h \
	TraceEventObserver.h \
	TraceEventType.h \
	TraceFieldType.h \
	RendezvousHash.h \
	ConsistentHash.h \
	URL.h \
	UtilInl.h \
	Logging.h \
	ZlibStreamCompressor.h \
	ZlibStreamDecompressor.h \
	WheelTimerInstance.h

# We put the generated files first so that we create them first
libutils_la_SOURCES = \
	../../external/http_parser/http_parser_cpp.cpp \
	AsyncTimeoutSet.cpp \
	Base64.cpp \
	ChromeUtils.cpp \
	Exception.cpp \
	HTTPTime.cpp \
	TraceEventContext.cpp \
	ParseURL.cpp \
	TraceEvent.cpp \
	TraceEventType.cpp \
	TraceFieldType.cpp \
	RendezvousHash.cpp \
	Logging.cpp \
	CryptUtil.cpp \
	ZlibStreamCompressor.cpp \
	ZlibStreamDecompressor.cpp \
	WheelTimerInstance.cpp
