#include <cstdint>

#include <gtest/gtest.h>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/storage/in_memory_table.hpp"
#include "curiodb/storage/row.hpp"
#include "curiodb/storage/row_validation.hpp"
#include "curiodb/types/data_type.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::storage {
namespace {

catalog::TableSchema employee_schema() {
  return {
      .name = "employees",
      .columns =
          {
              {.name = "id", .type = {.kind = DataTypeKind::Integer}},
              {.name = "name",
               .type = {.kind = DataTypeKind::Varchar, .length = 10}},
              {.name = "salary", .type = {.kind = DataTypeKind::Double}},
          },
  };
}

Row employee(std::int64_t id, const char* name, double salary) {
  return Row{Value{id}, Value{name}, Value{salary}};
}

TEST(InMemoryTableTest, StartsEmptyAndRetainsSchema) {
  InMemoryTable table{employee_schema()};

  EXPECT_TRUE(table.empty());
  EXPECT_EQ(table.row_count(), 0U);
  EXPECT_TRUE(table.rows().empty());
  EXPECT_EQ(table.schema(), employee_schema());
}

TEST(InMemoryTableTest, InsertsAndScansRowsInInsertionOrder) {
  InMemoryTable table{employee_schema()};

  EXPECT_FALSE(table.insert(employee(1, "Alice", 65000.0)).has_value());
  EXPECT_FALSE(table.insert(employee(2, "Bob", 72000.0)).has_value());

  EXPECT_FALSE(table.empty());
  ASSERT_EQ(table.row_count(), 2U);
  const auto rows = table.rows();
  EXPECT_EQ(rows[0], employee(1, "Alice", 65000.0));
  EXPECT_EQ(rows[1], employee(2, "Bob", 72000.0));
}

TEST(InMemoryTableTest, RejectsInvalidRowWithoutChangingTable) {
  InMemoryTable table{employee_schema()};
  ASSERT_FALSE(table.insert(employee(1, "Alice", 65000.0)).has_value());
  const Row invalid{Value{std::int64_t{2}}, Value{3.5}, Value{72000.0}};

  const auto result = table.insert(invalid);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, RowValidationErrorCode::TypeMismatch);
  ASSERT_EQ(table.row_count(), 1U);
  EXPECT_EQ(table.rows().front(), employee(1, "Alice", 65000.0));
}

TEST(InMemoryTableTest, RejectsTooLongVarcharWithoutChangingTable) {
  InMemoryTable table{employee_schema()};

  const auto result = table.insert(employee(1, "Christopher", 65000.0));

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, RowValidationErrorCode::VarcharTooLong);
  EXPECT_TRUE(table.empty());
}

TEST(InMemoryTableTest, ReturnedRowsAreReadOnly) {
  InMemoryTable table{employee_schema()};
  ASSERT_FALSE(table.insert(employee(1, "Alice", 65000.0)).has_value());

  const std::span<const Row> rows = table.rows();
  ASSERT_EQ(rows.size(), 1U);
  EXPECT_EQ(rows.front()[1].as_string(), "Alice");
}

}  // namespace
}  // namespace curiodb::storage

