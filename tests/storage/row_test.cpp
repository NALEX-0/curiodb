#include <cstdint>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "curiodb/storage/row.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::storage {
namespace {

TEST(RowTest, StoresAnOrderedSequenceOfValues) {
  const Row row{
      Value{std::int64_t{1}},
      Value{"Alice"},
      Value{65000.0},
  };

  EXPECT_FALSE(row.empty());
  ASSERT_EQ(row.size(), 3U);
  EXPECT_EQ(row[0].as_integer(), 1);
  EXPECT_EQ(row[1].as_string(), "Alice");
  EXPECT_DOUBLE_EQ(row[2].as_double(), 65000.0);
}

TEST(RowTest, ProvidesCheckedAccess) {
  const Row row{Value{std::int64_t{1}}};

  EXPECT_EQ(row.at(0).as_integer(), 1);
  EXPECT_THROW(static_cast<void>(row.at(1)), std::out_of_range);
}

TEST(RowTest, ExposesValuesAsReadOnlyCollection) {
  const Row row{std::vector<Value>{Value{std::int64_t{1}}, Value{"Alice"}}};

  ASSERT_EQ(row.values().size(), 2U);
  EXPECT_EQ(row.values()[1].as_string(), "Alice");
}

TEST(RowTest, SupportsEmptyRowsAndStructuralEquality) {
  const Row empty;
  EXPECT_TRUE(empty.empty());

  const Row first{Value{std::int64_t{1}}, Value{"Alice"}};
  const Row second{Value{std::int64_t{1}}, Value{"Alice"}};
  EXPECT_EQ(first, second);
}

}  // namespace
}  // namespace curiodb::storage

