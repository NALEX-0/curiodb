#pragma once

#include <string>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/sql/ast.hpp"

namespace curiodb::execution {

struct ExecutionResult {
  bool success;
  std::string message;

  [[nodiscard]] friend bool operator==(
      const ExecutionResult&, const ExecutionResult&) = default;
};

class StatementExecutor {
 public:
  explicit StatementExecutor(catalog::Catalog& catalog);

  [[nodiscard]] ExecutionResult execute(const sql::Statement& statement);

 private:
  [[nodiscard]] ExecutionResult execute_create_database(
      const sql::CreateDatabaseStatement& statement);
  [[nodiscard]] ExecutionResult execute_use_database(
      const sql::UseDatabaseStatement& statement);
  [[nodiscard]] ExecutionResult execute_create_table(
      const sql::CreateTableStatement& statement);

  catalog::Catalog& catalog_;
};

}  // namespace curiodb::execution
