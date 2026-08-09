#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "curiodb/sql/token.hpp"

namespace curiodb::sql {

enum class DataTypeKind {
  Integer,
  Double,
  Varchar,
};

struct DataType {
  DataTypeKind kind{DataTypeKind::Integer};
  std::optional<std::size_t> length;

  [[nodiscard]] friend bool operator==(const DataType&, const DataType&) =
      default;
};

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

