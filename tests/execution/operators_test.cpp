#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/execution/operators.hpp"
#include "curiodb/storage/in_memory_table.hpp"
#include "curiodb/storage/row.hpp"
#include "curiodb/types/data_type.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::execution {
namespace {

storage::InMemoryTable employees() {
  storage::InMemoryTable table{catalog::TableSchema{
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}},
                  {.name = "name",
                   .type = {.kind = DataTypeKind::Varchar, .length = 10}}},
  }};
  static_cast<void>(table.insert(
      storage::Row{Value{std::int64_t{1}}, Value{"Alice"}}));
  static_cast<void>(
      table.insert(storage::Row{Value{std::int64_t{2}}, Value{"Bob"}}));
  return table;
}

TEST(SequentialScanOperatorTest, ReturnsEveryRowInStorageOrder) {
  const auto table = employees();

  const RowSet rows = SequentialScanOperator{table}.execute();

  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].get()[1].as_string(), "Alice");
  EXPECT_EQ(rows[1].get()[1].as_string(), "Bob");
}

TEST(FilterOperatorTest, RetainsOnlyMatchingRows) {
  const auto table = employees();
  RowSet rows = SequentialScanOperator{table}.execute();

  rows = FilterOperator{
      std::move(rows),
      [](const storage::Row& row) { return row[0].as_integer() > 1; }}
             .execute();

  ASSERT_EQ(rows.size(), 1U);
  EXPECT_EQ(rows.front().get()[1].as_string(), "Bob");
}

TEST(ProjectionOperatorTest, SelectsColumnsInRequestedOrder) {
  const auto table = employees();
  RowSet rows = SequentialScanOperator{table}.execute();

  const auto projected =
      ProjectionOperator{std::move(rows), {1, 0}}.execute();

  EXPECT_EQ(projected,
            (std::vector<std::vector<std::string>>{
                {"Alice", "1"}, {"Bob", "2"}}));
}

TEST(OperatorPipelineTest, ScansFiltersThenProjects) {
  const auto table = employees();
  RowSet rows = SequentialScanOperator{table}.execute();
  rows = FilterOperator{
      std::move(rows),
      [](const storage::Row& row) { return row[1].as_string() == "Alice"; }}
             .execute();

  const auto projected = ProjectionOperator{std::move(rows), {0}}.execute();

  EXPECT_EQ(projected,
            (std::vector<std::vector<std::string>>{{"1"}}));
}

}  // namespace
}  // namespace curiodb::execution
