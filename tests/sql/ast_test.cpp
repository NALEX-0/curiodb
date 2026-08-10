#include <variant>

#include <gtest/gtest.h>

#include "curiodb/sql/ast.hpp"

namespace curiodb::sql {
namespace {

TEST(AstTest, RepresentsCreateDatabaseStatement) {
  const Statement statement = CreateDatabaseStatement{
      .name = "company",
      .location = {.offset = 0, .line = 1, .column = 1},
  };

  ASSERT_TRUE(std::holds_alternative<CreateDatabaseStatement>(statement));
  EXPECT_EQ(std::get<CreateDatabaseStatement>(statement).name, "company");
}

TEST(AstTest, RepresentsUseDatabaseStatement) {
  const Statement statement = UseDatabaseStatement{
      .name = "company",
      .location = {.offset = 10, .line = 2, .column = 1},
  };

  ASSERT_TRUE(std::holds_alternative<UseDatabaseStatement>(statement));
  EXPECT_EQ(std::get<UseDatabaseStatement>(statement).location,
            (SourceLocation{10, 2, 1}));
}

TEST(AstTest, RepresentsCreateTableWithTypedColumns) {
  const Statement statement = CreateTableStatement{
      .name = "employees",
      .columns =
          {
              ColumnDefinition{
                  .name = "id",
                  .type = {.kind = DataTypeKind::Integer},
                  .location = {.offset = 24, .line = 2, .column = 3},
              },
              ColumnDefinition{
                  .name = "name",
                  .type = {.kind = DataTypeKind::Varchar, .length = 100},
                  .location = {.offset = 34, .line = 3, .column = 3},
              },
              ColumnDefinition{
                  .name = "salary",
                  .type = {.kind = DataTypeKind::Double},
                  .location = {.offset = 55, .line = 4, .column = 3},
              },
          },
      .location = {.offset = 0, .line = 1, .column = 1},
  };

  const auto& create_table = std::get<CreateTableStatement>(statement);
  ASSERT_EQ(create_table.columns.size(), 3U);
  EXPECT_EQ(create_table.name, "employees");
  EXPECT_EQ(create_table.columns[0].type.kind, DataTypeKind::Integer);
  EXPECT_FALSE(create_table.columns[0].type.length.has_value());
  EXPECT_EQ(create_table.columns[1].type.kind, DataTypeKind::Varchar);
  EXPECT_EQ(create_table.columns[1].type.length, 100U);
  EXPECT_EQ(create_table.columns[2].type.kind, DataTypeKind::Double);
}

TEST(AstTest, SupportsStructuralEquality) {
  const CreateDatabaseStatement first{
      .name = "company",
      .location = {.offset = 0, .line = 1, .column = 1},
  };
  const CreateDatabaseStatement second = first;

  EXPECT_EQ(first, second);
}

TEST(AstTest, RepresentsInsertWithTypedLiterals) {
  const Statement statement = InsertStatement{
      .table_name = "employees",
      .values = {{.value = std::int64_t{1}},
                 {.value = std::string{"Alice"}},
                 {.value = 65000.0}},
      .location = {.offset = 0, .line = 1, .column = 1},
  };

  const auto& insert = std::get<InsertStatement>(statement);
  ASSERT_EQ(insert.values.size(), 3U);
  EXPECT_EQ(std::get<std::int64_t>(insert.values[0].value), 1);
  EXPECT_EQ(std::get<std::string>(insert.values[1].value), "Alice");
  EXPECT_DOUBLE_EQ(std::get<double>(insert.values[2].value), 65000.0);
}

TEST(AstTest, RepresentsSelectAll) {
  const Statement statement = SelectStatement{
      .columns = {},
      .table_name = "employees",
      .where = std::nullopt,
      .location = {.offset = 0, .line = 1, .column = 1},
  };

  EXPECT_EQ(std::get<SelectStatement>(statement).table_name, "employees");
}

TEST(AstTest, RepresentsProjectedColumns) {
  const Statement statement = SelectStatement{
      .columns = {{.name = "name"}, {.name = "salary"}},
      .table_name = "employees",
      .where = std::nullopt,
  };

  const auto& select = std::get<SelectStatement>(statement);
  ASSERT_EQ(select.columns.size(), 2U);
  EXPECT_EQ(select.columns[0].name, "name");
  EXPECT_EQ(select.columns[1].name, "salary");
}

TEST(AstTest, RepresentsWhereComparison) {
  const Statement statement = SelectStatement{
      .columns = {},
      .table_name = "employees",
      .where = ComparisonExpression{
          .column_name = "salary",
          .operation = ComparisonOperator::GreaterThan,
          .value = {.value = 70000.0},
      },
  };

  const auto& where = std::get<SelectStatement>(statement).where;
  ASSERT_TRUE(where.has_value());
  EXPECT_EQ(where->column_name, "salary");
  EXPECT_EQ(where->operation, ComparisonOperator::GreaterThan);
  EXPECT_DOUBLE_EQ(std::get<double>(where->value.value), 70000.0);
}

}  // namespace
}  // namespace curiodb::sql
