#include <variant>

#include <gtest/gtest.h>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/execution/statement_executor.hpp"
#include "curiodb/sql/ast.hpp"

namespace curiodb::execution {
namespace {

TEST(StatementExecutorTest, AppliesStatementsToCatalog) {
  catalog::Catalog catalog;
  StatementExecutor executor{catalog};

  EXPECT_EQ(executor.execute(sql::CreateDatabaseStatement{.name = "company"}),
            (ExecutionResult{true, "Database 'company' created."}));
  EXPECT_EQ(executor.execute(sql::UseDatabaseStatement{.name = "company"}),
            (ExecutionResult{true, "Using database 'company'."}));
  EXPECT_EQ(
      executor.execute(sql::CreateTableStatement{
          .name = "employees",
          .columns = {{.name = "id",
                       .type = {.kind = DataTypeKind::Integer}},
                      {.name = "name",
                       .type = {.kind = DataTypeKind::Varchar, .length = 100}}},
      }),
      (ExecutionResult{true, "Table 'employees' created."}));

  const auto* table = catalog.find_table("employees");
  ASSERT_NE(table, nullptr);
  ASSERT_EQ(table->columns.size(), 2U);
  EXPECT_EQ(table->columns[1].type.length, 100U);
}

TEST(StatementExecutorTest, ReturnsCatalogErrors) {
  catalog::Catalog catalog;
  StatementExecutor executor{catalog};

  const auto result = executor.execute(sql::CreateTableStatement{
      .name = "employees",
      .columns = {{.name = "id",
                   .type = {.kind = DataTypeKind::Integer}}},
  });

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.message,
            "no database selected; use USE <database> first");
}

}  // namespace
}  // namespace curiodb::execution
