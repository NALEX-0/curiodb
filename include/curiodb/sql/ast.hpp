#pragma once

#include <string>
#include <variant>
#include <vector>

#include "curiodb/sql/token.hpp"
#include "curiodb/types/data_type.hpp"

namespace curiodb::sql {

struct ColumnDefinition {
  std::string name;
  DataType type;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const ColumnDefinition&, const ColumnDefinition&) = default;
};

struct CreateDatabaseStatement {
  std::string name;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const CreateDatabaseStatement&, const CreateDatabaseStatement&) =
      default;
};

struct UseDatabaseStatement {
  std::string name;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const UseDatabaseStatement&, const UseDatabaseStatement&) = default;
};

struct CreateTableStatement {
  std::string name;
  std::vector<ColumnDefinition> columns;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const CreateTableStatement&, const CreateTableStatement&) = default;
};

using Statement =
    std::variant<CreateDatabaseStatement, UseDatabaseStatement,
                 CreateTableStatement>;

}  // namespace curiodb::sql
