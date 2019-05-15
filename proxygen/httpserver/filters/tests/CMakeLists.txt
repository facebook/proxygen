proxygen_add_test(TARGET HTTPServerFilterTests
  SOURCES
    ZlibServerFilterTest.cpp
  DEPENDS
    proxygen
    proxygenhttpserver
    testmain
)
