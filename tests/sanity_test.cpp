#include <gtest/gtest.h>
#include "tradeflow/version.hpp"

TEST(Sanity, VersionIsSet) {
  EXPECT_STREQ(tradeflow::kVersion, "0.1.0");
}
