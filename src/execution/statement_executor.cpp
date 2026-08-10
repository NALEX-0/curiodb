#include "curiodb/execution/statement_executor.hpp"

#include <algorithm>
#include <cctype>
#include <compare>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "curiodb/execution/operators.hpp"
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

std::string normalize(std::string_view name) {
  std::string result;
  result.reserve(name.size());
  for (const char character : name) {
    result.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

Value value_from_literal(const sql::Literal& literal) {
  return std::visit([](const auto& value) { return Value{value}; },
                    literal.value);
}

std::string value_type_name(DataTypeKind type) {
  switch (type) {
    case DataTypeKind::Integer:
      return "INT";
    case DataTypeKind::Double:
      return "DOUBLE";
    case DataTypeKind::Varchar:
      return "VARCHAR";
  }
  return "UNKNOWN";
}

bool matches(const Value& left, sql::ComparisonOperator operation,
             const Value& right) {
  switch (operation) {
    case sql::ComparisonOperator::Equal:
      return left == right;
    case sql::ComparisonOperator::NotEqual:
      return left != right;
    case sql::ComparisonOperator::LessThan:
      return std::is_lt(left <=> right);
    case sql::ComparisonOperator::LessThanOrEqual:
      return std::is_lteq(left <=> right);
    case sql::ComparisonOperator::GreaterThan:
      return std::is_gt(left <=> right);
    case sql::ComparisonOperator::GreaterThanOrEqual:
      return std::is_gteq(left <=> right);
  }
  return false;
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
    values.push_back(value_from_literal(literal));
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
  std::optional<std::size_t> filter_index;
  std::optional<Value> filter_value;
  if (statement.where.has_value()) {
    const std::string filter_name = normalize(statement.where->column_name);
    const auto column = std::find_if(
        table->schema().columns.begin(), table->schema().columns.end(),
        [&filter_name](const catalog::ColumnSchema& candidate) {
          return normalize(candidate.name) == filter_name;
        });
    if (column == table->schema().columns.end()) {
      return {.success = false,
              .message = "column '" + statement.where->column_name +
                         "' does not exist"};
    }
    filter_index = static_cast<std::size_t>(
        std::distance(table->schema().columns.begin(), column));
    filter_value = value_from_literal(statement.where->value);
    if (filter_value->type() != column->type.kind) {
      return {.success = false,
              .message = "column '" + column->name + "': expected " +
                         format_data_type(column->type) + ", received " +
                         value_type_name(filter_value->type())};
    }
  }

  std::vector<std::size_t> column_indexes;
  if (statement.columns.empty()) {
    column_indexes.reserve(table->schema().columns.size());
    for (std::size_t index = 0; index < table->schema().columns.size(); ++index) {
      column_indexes.push_back(index);
    }
  } else {
    column_indexes.reserve(statement.columns.size());
    std::unordered_set<std::size_t> selected_indexes;
    for (const auto& selected : statement.columns) {
      const std::string selected_name = normalize(selected.name);
      const auto column = std::find_if(
          table->schema().columns.begin(), table->schema().columns.end(),
          [&selected_name](const catalog::ColumnSchema& candidate) {
            return normalize(candidate.name) == selected_name;
          });
      if (column == table->schema().columns.end()) {
        return {.success = false,
                .message = "column '" + selected.name + "' does not exist"};
      }
      const auto index = static_cast<std::size_t>(
          std::distance(table->schema().columns.begin(), column));
      if (!selected_indexes.insert(index).second) {
        return {.success = false,
                .message = "column '" + selected.name +
                           "' selected more than once"};
      }
      column_indexes.push_back(index);
    }
  }

  query.columns.reserve(column_indexes.size());
  for (const std::size_t index : column_indexes) {
    query.columns.push_back(table->schema().columns[index].name);
  }

  RowSet rows = SequentialScanOperator{*table}.execute();
  if (filter_index.has_value()) {
    const std::size_t index = *filter_index;
    const sql::ComparisonOperator operation = statement.where->operation;
    const Value comparison_value = *filter_value;
    rows = FilterOperator{
        std::move(rows),
        [index, operation,
         comparison_value](const storage::Row& row) {
          return matches(row[index], operation, comparison_value);
        }}
               .execute();
  }
  query.rows =
      ProjectionOperator{std::move(rows), std::move(column_indexes)}.execute();

  const std::size_t count = query.rows.size();
  return {
      .success = true,
      .message = std::to_string(count) + (count == 1 ? " row selected."
                                                      : " rows selected."),
      .query = std::move(query),
  };
}

}  // namespace curiodb::execution
