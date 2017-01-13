# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.65)

m4_define([proxygen_version_str], m4_esyscmd_s([cat VERSION]))

AC_INIT([proxygen], m4_translit(proxygen_version_str, [:], [.]))

LT_VERSION=proxygen_version_str:0
AC_SUBST([LT_VERSION])

AC_CONFIG_SRCDIR([lib/http/Window.h])
AC_CONFIG_HEADERS([config.h])
AX_PREFIX_CONFIG_H([proxygen-config.h], [proxygen], [config.h])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([1.9 foreign tar-ustar nostdinc subdir-objects])
AC_CONFIG_MACRO_DIR([m4])

PKG_PROG_PKG_CONFIG

AC_PROG_INSTALL
AC_PROG_LIBTOOL

AC_LANG([C++])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_RANLIB
AX_CXX_COMPILE_STDCXX_14

CXXFLAGS="-fno-strict-aliasing $CXXFLAGS"
CXXFLAGS="-O3 -g -W -Wall -Wextra -Wno-unused-parameter $CXXFLAGS"
CXXFLAGS=" -Wno-missing-field-initializers -Wno-deprecated $CXXFLAGS"
CXXFLAGS="-DLIBMC_FBTRACE_DISABLE $CXXFLAGS"
CXXFLAGS="-pthread $CXXFLAGS"

CFLAGS="-DLIBMC_FBTRACE_DISABLE $CFLAGS"

# Checks for libraries.
AC_CHECK_LIB([glog],[openlog],[],[AC_MSG_ERROR(
             [Please install google-glog library])])
AC_CHECK_LIB([gflags],[getenv],[],AC_MSG_ERROR(
             [Please install google-gflags library]))

# check for boost libs
AX_BOOST_BASE([1.51.0], [], [AC_MSG_ERROR(
              [Please install boost >= 1.51.0])])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h stdint.h string.h sys/file.h sys/time.h syslog.h unistd.h malloc.h])

AC_CHECK_LIB([event], [event_set], [], [AC_MSG_ERROR([Unable to find libevent])])
AC_CHECK_LIB([cap], [cap_get_proc], [], [AC_MSG_ERROR([Unable to find libcap])])
AC_CHECK_LIB([crypto], [MD5_Init], [], [AC_MSG_ERROR([Unable to find libcrypto])])
AC_CHECK_LIB([ssl], [SSL_library_init], [], [AC_MSG_ERROR([Unable to find libssl])])
AC_CHECK_LIB([z], [gzread], [], [AC_MSG_ERROR([Unable to find zlib])])
AC_CHECK_LIB([folly],[getenv],[],[AC_MSG_ERROR(
             [Please install the folly library from https://github.com/facebook/folly])])
AC_CHECK_LIB([wangle], [getenv], [], [AC_MSG_ERROR(
             [Please install the wangle library from https://github.com/facebook/wangle])])
AC_CHECK_HEADER([folly/Likely.h], [], [AC_MSG_ERROR(
[Couldn't find folly, please download from https://github.com/facebook/folly]
)], [])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_C_VOLATILE
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

AC_CHECK_TYPE([__int128],
  [AC_DEFINE([HAVE_INT128_T], [1], [Define if __int128 exists])],
  [AC_DEFINE([HAVE_INT128_T], [0], [Define if __int128 does not exist])])
AC_CHECK_TYPES([ptrdiff_t])
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[
    #pragma GCC diagnostic error "-Wattributes"
    extern "C" void (*test_ifunc(void))() { return 0; }
    void func() __attribute__((ifunc("test_ifunc")));]
  ],
  [AC_DEFINE([HAVE_IFUNC], [1], [Define to 1 if the compiler supports ifunc])],
  [AC_DEFINE([HAVE_IFUNC], [0], [Define to 0 if the compiler doesn't support ifunc])]
)
AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[class C { virtual void f() final {} virtual void g() {} };
                  class D : public C { virtual void g() override {} };]],
  [AC_DEFINE([FINAL], [final],
             [Define to "final" if the compiler supports C++11 "final"])
   AC_DEFINE([OVERRIDE], [override],
             [Define to "override" if the compiler supports C++11 "override"])],
  [AC_DEFINE([FINAL], [],
             [Define to "final" if the compiler supports C++11 "final"])
   AC_DEFINE([OVERRIDE], [],
             [Define to "override" if the compiler supports C++11 "override"])]
)

AC_MSG_CHECKING([for static_assert])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([static_assert (1, "");],
                                   [(void) 0])],
                  [AC_DEFINE([HAVE_STATIC_ASSERT], [1],
                             [Whether static_assert can be used or not])
                   AC_MSG_RESULT([yes])],
                  [AC_MSG_RESULT([no])])

AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[
    #include <thread>
    #include <chrono>
    void func() { std::this_thread::sleep_for(std::chrono::seconds(1)); }]],
  [AC_DEFINE([HAVE_STD__THIS_THREAD__SLEEP_FOR], [1],
             [Define to 1 if std::this_thread::sleep_for() is defined.])])

AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[
    #include <cstring>
    static constexpr int val = strlen("foo");]],
  [AC_DEFINE([HAVE_CONSTEXPR_STRLEN], [1],
             [Define to 1 if strlen(3) is constexpr.])])

AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[
    #include <type_traits>
    #if !_LIBCPP_VERSION
    #error No libc++
    #endif
    void func() {}]
  ],
  [AC_DEFINE([USE_LIBCPP], [1], [Define to 1 if we're using libc++.])])

AC_COMPILE_IFELSE(
  [AC_LANG_SOURCE[
    #include <type_traits>
    const bool val = std::is_trivially_copyable<bool>::value;]
  ],
  [AC_DEFINE([HAVE_STD__IS_TRIVIALLY_COPYABLE], [1],
             [Define to 1 if we have a usable std::is_trivially_copyable<T>
              implementation.])])

# Figure out if we support weak symbols. If not, we will link in some null
# stubs for functions that would otherwise be weak.
AC_LINK_IFELSE(
  [AC_LANG_SOURCE[
    extern "C" void configure_link_extern_weak_test() __attribute__((weak));
    int main(int argc, char** argv) {
        return configure_link_extern_weak_test == nullptr;
    }]
  ],
  [
    ac_have_weak_symbols="yes"
    AC_DEFINE([HAVE_WEAK_SYMBOLS], [1],
              [Define to 1 if the linker supports weak symbols.])])

AC_SEARCH_LIBS([cplus_demangle_v3_callback], [iberty])
if test "$ac_cv_search_cplus_demangle_v3_callback" != "no" ; then
  AC_DEFINE([HAVE_CPLUS_DEMANGLE_V3_CALLBACK], [1],
            [Define to 1 if we have cplus_demangle_v3_callback.])
fi

# Check for clock_gettime(2). This is not in an AC_CHECK_FUNCS() because we
# want to link with librt if necessary.
AC_SEARCH_LIBS([clock_gettime], [rt],
  AC_DEFINE(
    [HAVE_CLOCK_GETTIME],
    [1],
    [Define to 1 if we support clock_gettime(2).]),
  [])

# Checks for library functions.
AC_CHECK_FUNCS([gettimeofday \
                localtime_r \
                memchr \
                memset \
                mkdir \
                socket \
                strcasecmp \
                strdup \
                strerror \
                strtol \
                dup2 \
                ftruncate])

# Check for python and gperf (needed for auto generated code)
AC_CHECK_PROG(HAVE_PYTHON, python, yes)
if test x"$HAVE_PYTHON" != x"yes"; then
   AC_MSG_ERROR([Please install python first.])
fi

AC_CHECK_PROG(HAVE_GPERF, gperf, yes)
if test x"$HAVE_GPERF" != x"yes"; then
   AC_MSG_ERROR([Please install gperf first.])
fi

LIBS="$LIBS $BOOST_LDFLAGS -lpthread -pthread -lfolly -lglog"
LIBS="$LIBS -ldouble-conversion -lboost_system -lboost_thread"

AM_CONDITIONAL([HAVE_STD_THREAD], [test "$ac_cv_header_features" = "yes"])
AM_CONDITIONAL([HAVE_X86_64], [test "$build_cpu" = "x86_64"])
AM_CONDITIONAL([HAVE_LINUX], [test "$build_os" == "linux-gnu"])
AM_CONDITIONAL([HAVE_WEAK_SYMBOLS], [test "$ac_have_weak_symbols" = "yes"])
AM_CONDITIONAL([HAVE_BITS_FUNCTEXCEPT_H], [test "$ac_cv_header_bits_functexcept" = "yes"])

# Include directory that contains "proxygen" so #include "proxygen/Foo.h" works
# Also add includes for gmock and gtest
AM_CPPFLAGS='-I$(top_srcdir)/.. -I$(top_srcdir)/lib/test/googletest-release-1.8.0/googlemock/include -I$(top_srcdir)/lib/test/googletest-release-1.8.0/googletest/include'
AM_CPPFLAGS="$AM_CPPFLAGS $CXX_FLAGS $BOOST_CPPFLAGS"
AC_SUBST([AM_CPPFLAGS])

# Output
AC_CONFIG_FILES([Makefile
                 lib/Makefile
                 lib/test/Makefile
                 lib/utils/Makefile
                 lib/utils/test/Makefile
                 lib/services/Makefile
                 lib/http/Makefile
                 lib/http/codec/Makefile
                 lib/http/codec/test/Makefile
                 lib/http/session/Makefile
                 lib/http/session/test/Makefile
                 lib/http/test/Makefile
                 httpserver/Makefile
                 httpserver/samples/Makefile
                 httpserver/samples/echo/Makefile
                 httpserver/samples/proxy/Makefile
                 httpserver/tests/Makefile
                 httpclient/Makefile
                 httpclient/samples/curl/Makefile
                 httpclient/samples/Makefile])

AC_OUTPUT
