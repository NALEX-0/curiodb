#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/execution/statement_executor.hpp"
#include "curiodb/sql/ast.hpp"
#include "curiodb/storage/in_memory_storage.hpp"

namespace curiodb::execution {
namespace {

sql::Expression equals(std::string column, sql::LiteralValue value) {
  return sql::Expression{sql::ComparisonExpression{
      .column_name = std::move(column),
      .operation = sql::ComparisonOperator::Equal,
      .value = {.value = std::move(value)},
  }};
}

sql::Expression logical(sql::LogicalOperator operation, sql::Expression left,
                        sql::Expression right) {
  return sql::Expression{std::make_shared<sql::LogicalExpression>(
      sql::LogicalExpression{.operation = operation,
                             .left = std::move(left),
                             .right = std::move(right)})};
}

TEST(StatementExecutorTest, AppliesStatementsToCatalog) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};

  EXPECT_EQ(executor.execute(sql::CreateDatabaseStatement{.name = "company"}),
            (ExecutionResult{true, "Database 'company' created.", std::nullopt}));
  EXPECT_EQ(executor.execute(sql::UseDatabaseStatement{.name = "company"}),
            (ExecutionResult{true, "Using database 'company'.", std::nullopt}));
  EXPECT_EQ(
      executor.execute(sql::CreateTableStatement{
          .name = "employees",
          .columns = {{.name = "id",
                       .type = {.kind = DataTypeKind::Integer}},
                      {.name = "name",
                       .type = {.kind = DataTypeKind::Varchar, .length = 100}}},
      }),
      (ExecutionResult{true, "Table 'employees' created.", std::nullopt}));

  const auto* table = catalog.find_table("employees");
  ASSERT_NE(table, nullptr);
  ASSERT_EQ(table->columns.size(), 2U);
  EXPECT_EQ(table->columns[1].type.length, 100U);
}

TEST(StatementExecutorTest, ReturnsCatalogErrors) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};

  const auto result = executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id",
                   .type = {.kind = DataTypeKind::Integer}}},
  });

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.message,
            "no database selected; use USE <database> first");
}

TEST(StatementExecutorTest, InsertsValidatedRowsIntoStorage) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};
  ASSERT_TRUE(executor.execute(
      sql::CreateDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(
      sql::UseDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}},
                  {.name = "name",
                   .type = {.kind = DataTypeKind::Varchar, .length = 10}}},
  }).success);

  const auto result = executor.execute(sql::InsertStatement{
      .table_name = "employees",
      .values = {{.value = std::int64_t{1}},
                 {.value = std::string{"Alice"}}},
  });

  EXPECT_EQ(result,
            (ExecutionResult{true, "1 row inserted.", std::nullopt}));
  const auto* table = storage.find_table("company", "employees");
  ASSERT_NE(table, nullptr);
  ASSERT_EQ(table->row_count(), 1U);
  EXPECT_EQ(table->rows().front()[1].as_string(), "Alice");
}

TEST(StatementExecutorTest, RejectsInvalidInsertWithoutAddingRow) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};
  ASSERT_TRUE(executor.execute(
      sql::CreateDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(
      sql::UseDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}}},
  }).success);

  const auto result = executor.execute(sql::InsertStatement{
      .table_name = "employees",
      .values = {{.value = std::string{"wrong"}}},
  });

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.message, "column 'id': expected INT, received VARCHAR");
  EXPECT_TRUE(storage.find_table("company", "employees")->empty());
}

TEST(StatementExecutorTest, SelectsAllRowsWithColumnNames) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};
  ASSERT_TRUE(executor.execute(
      sql::CreateDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(
      sql::UseDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}},
                  {.name = "name",
                   .type = {.kind = DataTypeKind::Varchar, .length = 10}}},
  }).success);
  ASSERT_TRUE(executor.execute(sql::InsertStatement{
      .table_name = "employees",
      .values = {{.value = std::int64_t{1}},
                 {.value = std::string{"Alice"}}},
  }).success);

  const auto result = executor.execute(
      sql::SelectStatement{.columns = {}, .table_name = "employees"});

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.message, "1 row selected.");
  ASSERT_TRUE(result.query.has_value());
  EXPECT_EQ(result.query->columns,
            (std::vector<std::string>{"id", "name"}));
  EXPECT_EQ(result.query->rows,
            (std::vector<std::vector<std::string>>{{"1", "Alice"}}));
}

TEST(StatementExecutorTest, SelectsFromEmptyTable) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};
  ASSERT_TRUE(executor.execute(
      sql::CreateDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(
      sql::UseDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}}},
  }).success);

  const auto result = executor.execute(
      sql::SelectStatement{.columns = {}, .table_name = "employees"});

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.message, "0 rows selected.");
  ASSERT_TRUE(result.query.has_value());
  EXPECT_TRUE(result.query->rows.empty());
}

TEST(StatementExecutorTest, ProjectsColumnsCaseInsensitivelyInRequestedOrder) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};
  ASSERT_TRUE(executor.execute(
      sql::CreateDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(
      sql::UseDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}},
                  {.name = "name",
                   .type = {.kind = DataTypeKind::Varchar, .length = 10}},
                  {.name = "salary",
                   .type = {.kind = DataTypeKind::Double}}},
  }).success);
  ASSERT_TRUE(executor.execute(sql::InsertStatement{
      .table_name = "employees",
      .values = {{.value = std::int64_t{1}},
                 {.value = std::string{"Alice"}},
                 {.value = 65000.0}},
  }).success);

  const auto result = executor.execute(sql::SelectStatement{
      .columns = {{.name = "SALARY"}, {.name = "Name"}},
      .table_name = "employees",
  });

  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.query.has_value());
  EXPECT_EQ(result.query->columns,
            (std::vector<std::string>{"salary", "name"}));
  EXPECT_EQ(result.query->rows,
            (std::vector<std::vector<std::string>>{{"65000", "Alice"}}));
}

TEST(StatementExecutorTest, RejectsUnknownAndDuplicateProjectedColumns) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};
  ASSERT_TRUE(executor.execute(
      sql::CreateDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(
      sql::UseDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}}},
  }).success);

  const auto unknown = executor.execute(sql::SelectStatement{
      .columns = {{.name = "missing"}}, .table_name = "employees"});
  EXPECT_FALSE(unknown.success);
  EXPECT_EQ(unknown.message, "column 'missing' does not exist");

  const auto duplicate = executor.execute(sql::SelectStatement{
      .columns = {{.name = "id"}, {.name = "ID"}},
      .table_name = "employees",
  });
  EXPECT_FALSE(duplicate.success);
  EXPECT_EQ(duplicate.message, "column 'ID' selected more than once");
}

TEST(StatementExecutorTest, FiltersRowsBeforeProjection) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};
  ASSERT_TRUE(executor.execute(
      sql::CreateDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(
      sql::UseDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}},
                  {.name = "name",
                   .type = {.kind = DataTypeKind::Varchar, .length = 10}},
                  {.name = "salary",
                   .type = {.kind = DataTypeKind::Double}}},
  }).success);
  ASSERT_TRUE(executor.execute(sql::InsertStatement{
      .table_name = "employees",
      .values = {{.value = std::int64_t{1}},
                 {.value = std::string{"Alice"}},
                 {.value = 65000.0}},
  }).success);
  ASSERT_TRUE(executor.execute(sql::InsertStatement{
      .table_name = "employees",
      .values = {{.value = std::int64_t{2}},
                 {.value = std::string{"Bob"}},
                 {.value = 72000.0}},
  }).success);

  const auto result = executor.execute(sql::SelectStatement{
      .columns = {{.name = "name"}},
      .table_name = "employees",
      .where = sql::Expression{sql::ComparisonExpression{
          .column_name = "SALARY",
          .operation = sql::ComparisonOperator::GreaterThan,
          .value = {.value = 70000.0},
      }},
  });

  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.query.has_value());
  EXPECT_EQ(result.query->columns, (std::vector<std::string>{"name"}));
  EXPECT_EQ(result.query->rows,
            (std::vector<std::vector<std::string>>{{"Bob"}}));
  EXPECT_EQ(result.message, "1 row selected.");
}

TEST(StatementExecutorTest, RejectsWhereValueWithWrongType) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};
  ASSERT_TRUE(executor.execute(
      sql::CreateDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(
      sql::UseDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}}},
  }).success);

  const auto result = executor.execute(sql::SelectStatement{
      .columns = {},
      .table_name = "employees",
      .where = sql::Expression{sql::ComparisonExpression{
          .column_name = "id",
          .operation = sql::ComparisonOperator::Equal,
          .value = {.value = std::string{"one"}},
      }},
  });

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.message, "column 'id': expected INT, received VARCHAR");
}

TEST(StatementExecutorTest, EvaluatesNestedBooleanExpressions) {
  catalog::Catalog catalog;
  storage::InMemoryStorage storage;
  StatementExecutor executor{catalog, storage};
  ASSERT_TRUE(executor.execute(
      sql::CreateDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(
      sql::UseDatabaseStatement{.name = "company"}).success);
  ASSERT_TRUE(executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id", .type = {.kind = DataTypeKind::Integer}},
                  {.name = "name",
                   .type = {.kind = DataTypeKind::Varchar, .length = 10}}},
  }).success);
  ASSERT_TRUE(executor.execute(sql::InsertStatement{
      .table_name = "employees",
      .values = {{.value = std::int64_t{1}},
                 {.value = std::string{"Alice"}}},
  }).success);
  ASSERT_TRUE(executor.execute(sql::InsertStatement{
      .table_name = "employees",
      .values = {{.value = std::int64_t{2}},
                 {.value = std::string{"Bob"}}},
  }).success);

  auto expression = logical(
      sql::LogicalOperator::And,
      logical(sql::LogicalOperator::Or, equals("id", std::int64_t{1}),
              equals("id", std::int64_t{2})),
      equals("name", std::string{"Bob"}));
  const auto result = executor.execute(sql::SelectStatement{
      .columns = {{.name = "name"}},
      .table_name = "employees",
      .where = std::move(expression),
  });

  ASSERT_TRUE(result.success);
  ASSERT_TRUE(result.query.has_value());
  EXPECT_EQ(result.query->rows,
            (std::vector<std::vector<std::string>>{{"Bob"}}));
}

}  // namespace
}  // namespace curiodb::execution
