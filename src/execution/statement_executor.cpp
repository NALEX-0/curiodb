#include "curiodb/execution/statement_executor.hpp"

#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "curiodb/storage/row.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::execution {
namespace {

ExecutionResult from_catalog_result(catalog::CatalogResult result,
                                    std::string success_message) {
  if (result.has_value()) {
    return {.success = false, .message = std::move(result->message)};
  }
  return {.success = true, .message = std::move(success_message)};
}

}  // namespace

StatementExecutor::StatementExecutor(catalog::Catalog& catalog,
                                     storage::InMemoryStorage& storage)
    : catalog_(catalog), storage_(storage) {}

ExecutionResult StatementExecutor::execute(const sql::Statement& statement) {
  return std::visit(
      [this](const auto& value) -> ExecutionResult {
        using StatementType = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<StatementType,
                                     sql::CreateDatabaseStatement>) {
          return execute_create_database(value);
        } else if constexpr (std::is_same_v<StatementType,
                                            sql::UseDatabaseStatement>) {
          return execute_use_database(value);
        } else if constexpr (std::is_same_v<StatementType,
                                            sql::CreateTableStatement>) {
          return execute_create_table(value);
        } else if constexpr (std::is_same_v<StatementType,
                                            sql::InsertStatement>) {
          return execute_insert(value);
        } else {
          return execute_select(value);
        }
      },
      statement);
}

ExecutionResult StatementExecutor::execute_create_database(
    const sql::CreateDatabaseStatement& statement) {
  return from_catalog_result(catalog_.create_database(statement.name),
                             "Database '" + statement.name + "' created.");
}

ExecutionResult StatementExecutor::execute_use_database(
    const sql::UseDatabaseStatement& statement) {
  return from_catalog_result(catalog_.use_database(statement.name),
                             "Using database '" + statement.name + "'.");
}

ExecutionResult StatementExecutor::execute_create_table(
    const sql::CreateTableStatement& statement) {
  std::vector<catalog::ColumnSchema> columns;
  columns.reserve(statement.columns.size());
  for (const auto& column : statement.columns) {
    columns.push_back({.name = column.name, .type = column.type});
  }
  const auto result = catalog_.create_table(statement.name, std::move(columns));
  if (result.has_value()) {
    return {.success = false, .message = result->message};
  }

  const auto database = catalog_.active_database();
  const catalog::TableSchema* const schema = catalog_.find_table(statement.name);
  if (!database.has_value() || schema == nullptr) {
    return {.success = false,
            .message = "internal error while creating table storage"};
  }
  storage_.create_table(*database, *schema);
  return {.success = true,
          .message = "Table '" + statement.name + "' created."};
}

ExecutionResult StatementExecutor::execute_insert(
    const sql::InsertStatement& statement) {
  const auto database = catalog_.active_database();
  if (!database.has_value()) {
    return {.success = false,
            .message = "no database selected; use USE <database> first"};
  }
  if (catalog_.find_table(statement.table_name) == nullptr) {
    return {.success = false,
            .message = "table '" + statement.table_name + "' does not exist"};
  }
  storage::InMemoryTable* const table =
      storage_.find_table(*database, statement.table_name);
  if (table == nullptr) {
    return {.success = false, .message = "table storage is unavailable"};
  }

  std::vector<Value> values;
  values.reserve(statement.values.size());
  for (const auto& literal : statement.values) {
    values.push_back(std::visit(
        [](const auto& value) { return Value{value}; }, literal.value));
  }
  if (auto error = table->insert(storage::Row{std::move(values)});
      error.has_value()) {
    return {.success = false, .message = std::move(error->message)};
  }
  return {.success = true, .message = "1 row inserted."};
}

ExecutionResult StatementExecutor::execute_select(
    const sql::SelectStatement& statement) {
  const auto database = catalog_.active_database();
  if (!database.has_value()) {
    return {.success = false,
            .message = "no database selected; use USE <database> first"};
  }
  if (catalog_.find_table(statement.table_name) == nullptr) {
    return {.success = false,
            .message = "table '" + statement.table_name + "' does not exist"};
  }
  const storage::InMemoryTable* const table =
      storage_.find_table(*database, statement.table_name);
  if (table == nullptr) {
    return {.success = false, .message = "table storage is unavailable"};
  }

  QueryResult query;
  query.columns.reserve(table->schema().columns.size());
  for (const auto& column : table->schema().columns) {
    query.columns.push_back(column.name);
  }
  query.rows.reserve(table->row_count());
  for (const auto& row : table->rows()) {
    std::vector<std::string> values;
    values.reserve(row.size());
    for (const auto& value : row.values()) {
      values.push_back(value.to_string());
    }
    query.rows.push_back(std::move(values));
  }

  const std::size_t count = query.rows.size();
  return {
      .success = true,
      .message = std::to_string(count) + (count == 1 ? " row selected."
                                                      : " rows selected."),
      .query = std::move(query),
  };
}

}  // namespace curiodb::execution
