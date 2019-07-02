proxygen_add_test(TARGET HTTPServerFilterTests
  SOURCES
  CompressionFilterTest.cpp
  DEPENDS
    proxygen
    proxygenhttpserver
    testmain
)
