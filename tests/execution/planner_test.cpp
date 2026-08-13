#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/execution/planner.hpp"
#include "curiodb/sql/ast.hpp"
#include "curiodb/storage/row.hpp"
#include "curiodb/types/data_type.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::execution {
namespace {

catalog::TableSchema employee_schema() {
  return {
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}},
                  {.name = "name",
                   .type = {.kind = DataTypeKind::Varchar, .length = 20}},
                  {.name = "salary",
                   .type = {.kind = DataTypeKind::Double}}},
  };
}

TEST(PlannerTest, BuildsScanFilterProjectionTree) {
  const auto result = plan_select(
      sql::SelectStatement{
          .columns = {{.name = "name"}, {.name = "id"}},
          .table_name = "employees",
          .where = sql::Expression{sql::ComparisonExpression{
              .column_name = "salary",
              .operation = sql::ComparisonOperator::GreaterThanOrEqual,
              .value = {.value = 50000.0},
          }}},
      employee_schema());

  ASSERT_TRUE(std::holds_alternative<PlanNode>(result));
  const auto& projection =
      std::get<ProjectionPlan>(std::get<PlanNode>(result).node);
  EXPECT_EQ(projection.column_indexes, (std::vector<std::size_t>{1, 0}));
  EXPECT_EQ(projection.column_names,
            (std::vector<std::string>{"name", "id"}));
  ASSERT_TRUE(std::holds_alternative<FilterPlan>(projection.child->node));
  const auto& filter = std::get<FilterPlan>(projection.child->node);
  EXPECT_EQ(filter.description, "salary >= 50000");
  ASSERT_TRUE(
      std::holds_alternative<SequentialScanPlan>(filter.child->node));
  EXPECT_EQ(std::get<SequentialScanPlan>(filter.child->node).table_name,
            "employees");
}

TEST(PlannerTest, FormatsReadableTree) {
  const auto result = plan_select(
      sql::SelectStatement{
          .columns = {{.name = "name"}},
          .table_name = "employees",
          .where = sql::Expression{sql::ComparisonExpression{
              .column_name = "id",
              .operation = sql::ComparisonOperator::Equal,
              .value = {.value = std::int64_t{42}},
          }}},
      employee_schema());
  ASSERT_TRUE(std::holds_alternative<PlanNode>(result));

  EXPECT_EQ(format_plan(std::get<PlanNode>(result)),
            "Projection(columns=name)\n"
            "  Filter(condition=id = 42)\n"
            "    SequentialScan(table=employees)");
}

TEST(PlannerTest, ExecutesPlannedFilterAndProjection) {
  const auto result = plan_select(
      sql::SelectStatement{
          .columns = {{.name = "name"}},
          .table_name = "employees",
          .where = sql::Expression{sql::ComparisonExpression{
              .column_name = "id",
              .operation = sql::ComparisonOperator::GreaterThan,
              .value = {.value = std::int64_t{1}},
          }}},
      employee_schema());
  ASSERT_TRUE(std::holds_alternative<PlanNode>(result));
  const storage::Row alice{Value{std::int64_t{1}}, Value{"Alice"},
                           Value{40000.0}};
  const storage::Row bob{Value{std::int64_t{2}}, Value{"Bob"},
                         Value{50000.0}};
  RowSet rows{std::cref(alice), std::cref(bob)};

  EXPECT_EQ(execute_plan(std::get<PlanNode>(result), std::move(rows)),
            (std::vector<std::vector<std::string>>{{"Bob"}}));
}

TEST(PlannerTest, RejectsUnknownAndDuplicateProjectionColumns) {
  const auto schema = employee_schema();
  const auto unknown = plan_select(
      sql::SelectStatement{.columns = {{.name = "missing"}},
                           .table_name = "employees"},
      schema);
  const auto duplicate = plan_select(
      sql::SelectStatement{.columns = {{.name = "id"}, {.name = "ID"}},
                           .table_name = "employees"},
      schema);

  ASSERT_TRUE(std::holds_alternative<PlannerError>(unknown));
  EXPECT_EQ(std::get<PlannerError>(unknown).message,
            "column 'missing' does not exist");
  ASSERT_TRUE(std::holds_alternative<PlannerError>(duplicate));
  EXPECT_EQ(std::get<PlannerError>(duplicate).message,
            "column 'ID' selected more than once");
}

TEST(PlannerTest, RejectsWhereLiteralWithWrongType) {
  const auto result = plan_select(
      sql::SelectStatement{
          .columns = {},
          .table_name = "employees",
          .where = sql::Expression{sql::ComparisonExpression{
              .column_name = "id",
              .operation = sql::ComparisonOperator::Equal,
              .value = {.value = std::string{"wrong"}},
          }}},
      employee_schema());

  ASSERT_TRUE(std::holds_alternative<PlannerError>(result));
  EXPECT_EQ(std::get<PlannerError>(result).message,
            "column 'id': expected INT, received VARCHAR");
}

}  // namespace
}  // namespace curiodb::execution
