#pragma once

#include <optional>
#include <string>
#include <vector>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/sql/ast.hpp"
#include "curiodb/storage/in_memory_storage.hpp"
#include "curiodb/storage/disk_storage.hpp"

namespace curiodb::execution {

struct QueryResult {
  std::vector<std::string> columns;
  std::vector<std::vector<std::string>> rows;

  [[nodiscard]] friend bool operator==(
      const QueryResult&, const QueryResult&) = default;
};

struct ExecutionResult {
  bool success;
  std::string message;
  std::optional<QueryResult> query;

  [[nodiscard]] friend bool operator==(
      const ExecutionResult&, const ExecutionResult&) = default;
};

class StatementExecutor {
 public:
  StatementExecutor(catalog::Catalog& catalog, storage::InMemoryStorage& storage);
  StatementExecutor(catalog::Catalog& catalog, storage::DiskStorage& storage);

  [[nodiscard]] ExecutionResult execute(const sql::Statement& statement);

 private:
  [[nodiscard]] ExecutionResult execute_create_database(
      const sql::CreateDatabaseStatement& statement);
  [[nodiscard]] ExecutionResult execute_use_database(
      const sql::UseDatabaseStatement& statement);
  [[nodiscard]] ExecutionResult execute_create_table(
      const sql::CreateTableStatement& statement);
  [[nodiscard]] ExecutionResult execute_insert(
      const sql::InsertStatement& statement);
  [[nodiscard]] ExecutionResult execute_select(
      const sql::SelectStatement& statement);

  catalog::Catalog& catalog_;
  storage::InMemoryStorage* in_memory_storage_{nullptr};
  storage::DiskStorage* disk_storage_{nullptr};
};

}  // namespace curiodb::execution
