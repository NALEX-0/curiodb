#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/types/data_type.hpp"

namespace curiodb::catalog {
namespace {

std::vector<ColumnSchema> employee_columns() {
  return {
      {.name = "id", .type = {.kind = DataTypeKind::Integer}},
      {.name = "name",
       .type = {.kind = DataTypeKind::Varchar, .length = 100}},
      {.name = "salary", .type = {.kind = DataTypeKind::Double}},
  };
}

TEST(CatalogTest, CreatesAndListsDatabasesInStableOrder) {
  Catalog catalog;

  EXPECT_FALSE(catalog.create_database("sales").has_value());
  EXPECT_FALSE(catalog.create_database("company").has_value());
  EXPECT_EQ(catalog.database_names(),
            (std::vector<std::string>{"company", "sales"}));
}

TEST(CatalogTest, SelectsDatabaseCaseInsensitively) {
  Catalog catalog;
  ASSERT_FALSE(catalog.create_database("Company").has_value());

  EXPECT_FALSE(catalog.use_database("COMPANY").has_value());
  EXPECT_EQ(catalog.active_database(), "Company");
}

TEST(CatalogTest, RejectsDuplicateDatabaseCaseInsensitively) {
  Catalog catalog;
  ASSERT_FALSE(catalog.create_database("company").has_value());

  const auto result = catalog.create_database("COMPANY");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, CatalogErrorCode::DatabaseAlreadyExists);
}

TEST(CatalogTest, RejectsUnknownDatabaseWithoutChangingSelection) {
  Catalog catalog;
  ASSERT_FALSE(catalog.create_database("company").has_value());
  ASSERT_FALSE(catalog.use_database("company").has_value());

  const auto result = catalog.use_database("missing");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, CatalogErrorCode::DatabaseNotFound);
  EXPECT_EQ(catalog.active_database(), "company");
}

TEST(CatalogTest, RequiresSelectedDatabaseBeforeCreatingTable) {
  Catalog catalog;

  const auto result = catalog.create_table("employees", employee_columns());

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, CatalogErrorCode::NoDatabaseSelected);
}

TEST(CatalogTest, CreatesFindsAndListsTables) {
  Catalog catalog;
  ASSERT_FALSE(catalog.create_database("company").has_value());
  ASSERT_FALSE(catalog.use_database("company").has_value());
  ASSERT_FALSE(
      catalog.create_table("employees", employee_columns()).has_value());
  ASSERT_FALSE(catalog.create_table(
                          "departments",
                          {{.name = "id",
                            .type = {.kind = DataTypeKind::Integer}}})
                   .has_value());

  EXPECT_EQ(catalog.table_names(),
            (std::vector<std::string>{"departments", "employees"}));
  const TableSchema* const table = catalog.find_table("EMPLOYEES");
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->name, "employees");
  EXPECT_EQ(table->columns, employee_columns());
}

TEST(CatalogTest, KeepsTablesIsolatedBetweenDatabases) {
  Catalog catalog;
  ASSERT_FALSE(catalog.create_database("company").has_value());
  ASSERT_FALSE(catalog.create_database("sales").has_value());
  ASSERT_FALSE(catalog.use_database("company").has_value());
  ASSERT_FALSE(
      catalog.create_table("employees", employee_columns()).has_value());
  ASSERT_FALSE(catalog.use_database("sales").has_value());

  EXPECT_TRUE(catalog.table_names().empty());
  EXPECT_EQ(catalog.find_table("employees"), nullptr);
}

TEST(CatalogTest, RejectsDuplicateTablesAndColumnsCaseInsensitively) {
  Catalog catalog;
  ASSERT_FALSE(catalog.create_database("company").has_value());
  ASSERT_FALSE(catalog.use_database("company").has_value());
  ASSERT_FALSE(
      catalog.create_table("employees", employee_columns()).has_value());

  const auto duplicate_table =
      catalog.create_table("EMPLOYEES", employee_columns());
  ASSERT_TRUE(duplicate_table.has_value());
  EXPECT_EQ(duplicate_table->code, CatalogErrorCode::TableAlreadyExists);

  const auto duplicate_column = catalog.create_table(
      "other", {{.name = "id", .type = {.kind = DataTypeKind::Integer}},
                {.name = "ID", .type = {.kind = DataTypeKind::Double}}});
  ASSERT_TRUE(duplicate_column.has_value());
  EXPECT_EQ(duplicate_column->code, CatalogErrorCode::DuplicateColumn);
  EXPECT_EQ(catalog.find_table("other"), nullptr);
}

TEST(CatalogTest, RejectsEmptyNamesAndColumnLists) {
  Catalog catalog;
  const auto empty_database = catalog.create_database("");
  ASSERT_TRUE(empty_database.has_value());
  EXPECT_EQ(empty_database->code, CatalogErrorCode::InvalidName);

  ASSERT_FALSE(catalog.create_database("company").has_value());
  ASSERT_FALSE(catalog.use_database("company").has_value());
  const auto empty_table = catalog.create_table("employees", {});
  ASSERT_TRUE(empty_table.has_value());
  EXPECT_EQ(empty_table->code, CatalogErrorCode::EmptyTable);
}

}  // namespace
}  // namespace curiodb::catalog

