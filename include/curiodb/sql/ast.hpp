#pragma once

#include <cstdint>
#include <optional>
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

using LiteralValue = std::variant<std::int64_t, double, std::string>;

struct Literal {
  LiteralValue value;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(const Literal&, const Literal&) = default;
};

struct InsertStatement {
  std::string table_name;
  std::vector<Literal> values;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const InsertStatement&, const InsertStatement&) = default;
};

enum class ComparisonOperator {
  Equal,
  NotEqual,
  LessThan,
  LessThanOrEqual,
  GreaterThan,
  GreaterThanOrEqual,
};

struct ComparisonExpression {
  std::string column_name;
  ComparisonOperator operation{ComparisonOperator::Equal};
  Literal value;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const ComparisonExpression&, const ComparisonExpression&) = default;
};

struct SelectStatement {
  struct Column {
    std::string name;
    SourceLocation location;

    [[nodiscard]] friend bool operator==(
        const Column&, const Column&) = default;
  };

  // An empty list represents SELECT *
  std::vector<Column> columns;
  std::string table_name;
  std::optional<ComparisonExpression> where;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const SelectStatement&, const SelectStatement&) = default;
};

using Statement =
    std::variant<CreateDatabaseStatement, UseDatabaseStatement,
                 CreateTableStatement, InsertStatement, SelectStatement>;

}  // namespace curiodb::sql
