#include "curiodb/execution/statement_executor.hpp"

#include <algorithm>
#include <cctype>
#include <compare>
#include <cstddef>
#include <functional>
#include <memory>
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
#include "curiodb/storage/row_validation.hpp"
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

std::optional<FilterOperator::Predicate> bind_predicate(
    const sql::Expression& expression, const catalog::TableSchema& schema,
    std::string& binding_error) {
  if (const auto* comparison =
          std::get_if<sql::ComparisonExpression>(&expression.node)) {
    const std::string filter_name = normalize(comparison->column_name);
    const auto column = std::find_if(
        schema.columns.begin(), schema.columns.end(),
        [&filter_name](const catalog::ColumnSchema& candidate) {
          return normalize(candidate.name) == filter_name;
        });
    if (column == schema.columns.end()) {
      binding_error = "column '" + comparison->column_name +
                      "' does not exist";
      return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(
        std::distance(schema.columns.begin(), column));
    const Value comparison_value = value_from_literal(comparison->value);
    if (comparison_value.type() != column->type.kind) {
      binding_error = "column '" + column->name + "': expected " +
                      format_data_type(column->type) + ", received " +
                      value_type_name(comparison_value.type());
      return std::nullopt;
    }
    const sql::ComparisonOperator operation = comparison->operation;
    return FilterOperator::Predicate{
        [index, operation, comparison_value](const storage::Row& row) {
          return matches(row[index], operation, comparison_value);
        }};
  }

  const auto& logical_pointer =
      std::get<std::shared_ptr<sql::LogicalExpression>>(expression.node);
  if (logical_pointer == nullptr) {
    binding_error = "invalid WHERE expression";
    return std::nullopt;
  }
  auto left = bind_predicate(logical_pointer->left, schema, binding_error);
  if (!left.has_value()) {
    return std::nullopt;
  }
  auto right = bind_predicate(logical_pointer->right, schema, binding_error);
  if (!right.has_value()) {
    return std::nullopt;
  }
  if (logical_pointer->operation == sql::LogicalOperator::And) {
    return FilterOperator::Predicate{
        [left = std::move(*left), right = std::move(*right)](
            const storage::Row& row) { return left(row) && right(row); }};
  }
  return FilterOperator::Predicate{
      [left = std::move(*left), right = std::move(*right)](
          const storage::Row& row) { return left(row) || right(row); }};
}

}  // namespace

StatementExecutor::StatementExecutor(catalog::Catalog& catalog,
                                     storage::InMemoryStorage& storage)
    : catalog_(catalog), in_memory_storage_(&storage) {}

StatementExecutor::StatementExecutor(catalog::Catalog& catalog,
                                     storage::DiskStorage& storage)
    : catalog_(catalog), disk_storage_(&storage) {}

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
        } else if constexpr (std::is_same_v<StatementType,
                                            sql::SelectStatement>) {
          return execute_select(value);
        } else if constexpr (std::is_same_v<StatementType,
                                            sql::DeleteStatement>) {
          return execute_delete(value);
        } else {
          return execute_update(value);
        }
      },
      statement);
}

ExecutionResult StatementExecutor::execute_create_database(
    const sql::CreateDatabaseStatement& statement) {
  auto result = catalog_.create_database(statement.name);
  if (result.has_value()) {
    return {.success = false, .message = std::move(result->message)};
  }
  if (disk_storage_ != nullptr) {
    const auto stored = disk_storage_->create_database(statement.name);
    if (const auto* storage_error =
            std::get_if<storage::DiskStorageError>(&stored)) {
      return {.success = false, .message = storage_error->message};
    }
  }
  return {.success = true,
          .message = "Database '" + statement.name + "' created."};
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
  if (disk_storage_ != nullptr) {
    const auto stored = disk_storage_->create_table(*database, *schema);
    if (const auto* storage_error =
            std::get_if<storage::DiskStorageError>(&stored)) {
      return {.success = false, .message = storage_error->message};
    }
  } else {
    in_memory_storage_->create_table(*database, *schema);
  }
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
  std::vector<Value> values;
  values.reserve(statement.values.size());
  for (const auto& literal : statement.values) {
    values.push_back(value_from_literal(literal));
  }
  storage::Row row{std::move(values)};
  if (disk_storage_ != nullptr) {
    const auto inserted =
        disk_storage_->insert(*database, statement.table_name, std::move(row));
    if (const auto* storage_error =
            std::get_if<storage::DiskStorageError>(&inserted)) {
      return {.success = false, .message = storage_error->message};
    }
  } else {
    storage::InMemoryTable* const table =
        in_memory_storage_->find_table(*database, statement.table_name);
    if (table == nullptr) {
      return {.success = false, .message = "table storage is unavailable"};
    }
    if (auto validation = table->insert(std::move(row));
        validation.has_value()) {
      return {.success = false, .message = std::move(validation->message)};
    }
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
  const catalog::TableSchema* const schema =
      catalog_.find_table(statement.table_name);
  if (schema == nullptr) {
    return {.success = false,
            .message = "table '" + statement.table_name + "' does not exist"};
  }
  std::vector<storage::Row> disk_rows;
  const storage::InMemoryTable* table = nullptr;
  if (disk_storage_ != nullptr) {
    auto scanned = disk_storage_->scan(*database, statement.table_name);
    if (const auto* storage_error =
            std::get_if<storage::DiskStorageError>(&scanned)) {
      return {.success = false, .message = storage_error->message};
    }
    disk_rows = std::get<std::vector<storage::Row>>(std::move(scanned));
  } else {
    table = in_memory_storage_->find_table(*database, statement.table_name);
    if (table == nullptr) {
      return {.success = false, .message = "table storage is unavailable"};
    }
  }

  QueryResult query;
  std::optional<FilterOperator::Predicate> predicate;
  if (statement.where.has_value()) {
    std::string binding_error;
    std::function<std::optional<FilterOperator::Predicate>(
        const sql::Expression&)>
        bind_expression;
    bind_expression = [&](const sql::Expression& expression)
        -> std::optional<FilterOperator::Predicate> {
      if (const auto* comparison =
              std::get_if<sql::ComparisonExpression>(&expression.node)) {
        const std::string filter_name = normalize(comparison->column_name);
        const auto column = std::find_if(
            schema->columns.begin(), schema->columns.end(),
            [&filter_name](const catalog::ColumnSchema& candidate) {
              return normalize(candidate.name) == filter_name;
            });
        if (column == schema->columns.end()) {
          binding_error =
              "column '" + comparison->column_name + "' does not exist";
          return std::nullopt;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(schema->columns.begin(), column));
        const Value comparison_value = value_from_literal(comparison->value);
        if (comparison_value.type() != column->type.kind) {
          binding_error = "column '" + column->name + "': expected " +
                          format_data_type(column->type) + ", received " +
                          value_type_name(comparison_value.type());
          return std::nullopt;
        }
        const sql::ComparisonOperator operation = comparison->operation;
        return FilterOperator::Predicate{
            [index, operation,
             comparison_value](const storage::Row& row) {
              return matches(row[index], operation, comparison_value);
            }};
      }

      const auto& logical_pointer =
          std::get<std::shared_ptr<sql::LogicalExpression>>(expression.node);
      if (logical_pointer == nullptr) {
        binding_error = "invalid WHERE expression";
        return std::nullopt;
      }
      const auto& logical = *logical_pointer;
      auto left = bind_expression(logical.left);
      if (!left.has_value()) {
        return std::nullopt;
      }
      auto right = bind_expression(logical.right);
      if (!right.has_value()) {
        return std::nullopt;
      }
      if (logical.operation == sql::LogicalOperator::And) {
        return FilterOperator::Predicate{
            [left = std::move(*left), right = std::move(*right)](
                const storage::Row& row) {
              return left(row) && right(row);
            }};
      }
      return FilterOperator::Predicate{
          [left = std::move(*left), right = std::move(*right)](
              const storage::Row& row) {
            return left(row) || right(row);
          }};
    };

    predicate = bind_expression(*statement.where);
    if (!predicate.has_value()) {
      return {.success = false, .message = std::move(binding_error)};
    }
  }

  std::vector<std::size_t> column_indexes;
  if (statement.columns.empty()) {
    column_indexes.reserve(schema->columns.size());
    for (std::size_t index = 0; index < schema->columns.size(); ++index) {
      column_indexes.push_back(index);
    }
  } else {
    column_indexes.reserve(statement.columns.size());
    std::unordered_set<std::size_t> selected_indexes;
    for (const auto& selected : statement.columns) {
      const std::string selected_name = normalize(selected.name);
      const auto column = std::find_if(
          schema->columns.begin(), schema->columns.end(),
          [&selected_name](const catalog::ColumnSchema& candidate) {
            return normalize(candidate.name) == selected_name;
          });
      if (column == schema->columns.end()) {
        return {.success = false,
                .message = "column '" + selected.name + "' does not exist"};
      }
      const auto index = static_cast<std::size_t>(
          std::distance(schema->columns.begin(), column));
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
    query.columns.push_back(schema->columns[index].name);
  }

  RowSet rows;
  if (table != nullptr) {
    rows = SequentialScanOperator{*table}.execute();
  } else {
    rows.reserve(disk_rows.size());
    for (const auto& row : disk_rows) {
      rows.emplace_back(std::cref(row));
    }
  }
  if (predicate.has_value()) {
    rows = FilterOperator{std::move(rows), std::move(*predicate)}.execute();
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

ExecutionResult StatementExecutor::execute_delete(
    const sql::DeleteStatement& statement) {
  const auto database = catalog_.active_database();
  if (!database.has_value()) {
    return {.success = false,
            .message = "no database selected; use USE <database> first"};
  }
  const catalog::TableSchema* const schema =
      catalog_.find_table(statement.table_name);
  if (schema == nullptr) {
    return {.success = false,
            .message = "table '" + statement.table_name + "' does not exist"};
  }

  FilterOperator::Predicate predicate = [](const storage::Row&) {
    return true;
  };
  if (statement.where.has_value()) {
    std::string binding_error;
    auto bound = bind_predicate(*statement.where, *schema, binding_error);
    if (!bound.has_value()) {
      return {.success = false, .message = std::move(binding_error)};
    }
    predicate = std::move(*bound);
  }

  std::size_t count = 0;
  if (disk_storage_ != nullptr) {
    const auto deleted = disk_storage_->delete_where(
        *database, statement.table_name, predicate);
    if (const auto* storage_error =
            std::get_if<storage::DiskStorageError>(&deleted)) {
      return {.success = false, .message = storage_error->message};
    }
    count = std::get<std::size_t>(deleted);
  } else {
    storage::InMemoryTable* const table =
        in_memory_storage_->find_table(*database, statement.table_name);
    if (table == nullptr) {
      return {.success = false, .message = "table storage is unavailable"};
    }
    count = table->delete_where(predicate);
  }
  return {.success = true,
          .message = std::to_string(count) +
                     (count == 1 ? " row deleted." : " rows deleted.")};
}

ExecutionResult StatementExecutor::execute_update(
    const sql::UpdateStatement& statement) {
  const auto database = catalog_.active_database();
  if (!database.has_value()) {
    return {.success = false,
            .message = "no database selected; use USE <database> first"};
  }
  const catalog::TableSchema* const schema =
      catalog_.find_table(statement.table_name);
  if (schema == nullptr) {
    return {.success = false,
            .message = "table '" + statement.table_name + "' does not exist"};
  }
  const std::string update_name = normalize(statement.column_name);
  const auto column = std::find_if(
      schema->columns.begin(), schema->columns.end(),
      [&update_name](const catalog::ColumnSchema& candidate) {
        return normalize(candidate.name) == update_name;
      });
  if (column == schema->columns.end()) {
    return {.success = false,
            .message = "column '" + statement.column_name +
                       "' does not exist"};
  }
  const std::size_t column_index = static_cast<std::size_t>(
      std::distance(schema->columns.begin(), column));
  const Value value = value_from_literal(statement.value);
  storage::Row validation_row;
  std::vector<Value> validation_values;
  validation_values.reserve(schema->columns.size());
  for (const auto& candidate : schema->columns) {
    switch (candidate.type.kind) {
      case DataTypeKind::Integer:
        validation_values.emplace_back(std::int64_t{0});
        break;
      case DataTypeKind::Double:
        validation_values.emplace_back(0.0);
        break;
      case DataTypeKind::Varchar:
        validation_values.emplace_back(std::string{});
        break;
    }
  }
  validation_values[column_index] = value;
  validation_row = storage::Row{std::move(validation_values)};
  if (const auto validation = storage::validate_row(*schema, validation_row);
      validation.has_value()) {
    return {.success = false, .message = validation->message};
  }

  FilterOperator::Predicate predicate = [](const storage::Row&) {
    return true;
  };
  if (statement.where.has_value()) {
    std::string binding_error;
    auto bound = bind_predicate(*statement.where, *schema, binding_error);
    if (!bound.has_value()) {
      return {.success = false, .message = std::move(binding_error)};
    }
    predicate = std::move(*bound);
  }

  std::size_t count = 0;
  if (disk_storage_ != nullptr) {
    const auto updated = disk_storage_->update_where(
        *database, statement.table_name, predicate, column_index, value);
    if (const auto* storage_error =
            std::get_if<storage::DiskStorageError>(&updated)) {
      return {.success = false, .message = storage_error->message};
    }
    count = std::get<std::size_t>(updated);
  } else {
    storage::InMemoryTable* const table =
        in_memory_storage_->find_table(*database, statement.table_name);
    if (table == nullptr) {
      return {.success = false, .message = "table storage is unavailable"};
    }
    count = table->update_where(predicate, column_index, value);
  }
  return {.success = true,
          .message = std::to_string(count) +
                     (count == 1 ? " row updated." : " rows updated.")};
}

}  // namespace curiodb::execution
