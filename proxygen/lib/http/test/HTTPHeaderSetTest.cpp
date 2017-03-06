#include <folly/portability/GTest.h>

#include <proxygen/lib/http/HTTPHeaderSet.h>

using namespace proxygen;

TEST(HTTPHeaderSet, constructors) {
  EXPECT_EQ(HTTPHeaderSet().to_ulong(), 0ul);
  HTTPHeaderSet s = { HTTP_HEADER_DATE, HTTP_HEADER_EXPIRES };
  HTTPHeaderSet s2 = s;
  EXPECT_EQ(s, s2);
  EXPECT_NE(s.to_ulong(), 0ul);

  HTTPHeaderSet s3(std::move(s2));
  EXPECT_EQ(s, s3);
}

TEST(HTTPHeaderSet, bits) {
  HTTPHeaderSet s { HTTP_HEADER_DATE, HTTP_HEADER_EXPIRES };
  EXPECT_TRUE(s.test(HTTP_HEADER_DATE));
  EXPECT_TRUE(s.test(HTTP_HEADER_EXPIRES));
  EXPECT_FALSE(s.test(HTTP_HEADER_VIA));
  EXPECT_FALSE(s.test(HTTP_HEADER_HOST));
}
