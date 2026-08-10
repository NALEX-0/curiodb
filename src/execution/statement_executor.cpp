#include "curiodb/execution/statement_executor.hpp"

#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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

StatementExecutor::StatementExecutor(catalog::Catalog& catalog)
    : catalog_(catalog) {}

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
        } else {
          return execute_create_table(value);
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
  return from_catalog_result(
      catalog_.create_table(statement.name, std::move(columns)),
      "Table '" + statement.name + "' created.");
}

}  // namespace curiodb::execution
