#include <gtest/gtest.h>

#include "curiodb/version.hpp"

TEST(VersionTest, ExposesProjectMetadata) {
  EXPECT_EQ(curiodb::kName, "CurioDB");
  EXPECT_EQ(curiodb::kVersion, "0.1.0");
}
