#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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

struct CreateIndexStatement {
  std::string name;
  std::string table_name;
  std::string column_name;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const CreateIndexStatement&, const CreateIndexStatement&) = default;
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

enum class LogicalOperator {
  And,
  Or,
};

struct LogicalExpression;

struct Expression {
  using Node =
      std::variant<ComparisonExpression, std::shared_ptr<LogicalExpression>>;

  explicit Expression(ComparisonExpression comparison)
      : node(std::move(comparison)) {}
  explicit Expression(std::shared_ptr<LogicalExpression> logical)
      : node(std::move(logical)) {}

  Node node;

  friend bool operator==(const Expression& left, const Expression& right);
};

struct LogicalExpression {
  LogicalOperator operation{LogicalOperator::And};
  Expression left;
  Expression right;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const LogicalExpression&, const LogicalExpression&) = default;
};

inline bool operator==(const Expression& left, const Expression& right) {
  if (left.node.index() != right.node.index()) {
    return false;
  }
  if (const auto* comparison =
          std::get_if<ComparisonExpression>(&left.node)) {
    return *comparison == std::get<ComparisonExpression>(right.node);
  }
  const auto& left_logical =
      std::get<std::shared_ptr<LogicalExpression>>(left.node);
  const auto& right_logical =
      std::get<std::shared_ptr<LogicalExpression>>(right.node);
  if (left_logical == nullptr || right_logical == nullptr) {
    return left_logical == right_logical;
  }
  return *left_logical == *right_logical;
}

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
  std::optional<Expression> where;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const SelectStatement&, const SelectStatement&) = default;
};

struct DeleteStatement {
  std::string table_name;
  std::optional<Expression> where;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const DeleteStatement&, const DeleteStatement&) = default;
};

struct ExplainStatement {
  SelectStatement select;

  [[nodiscard]] friend bool operator==(
      const ExplainStatement&, const ExplainStatement&) = default;
};

struct UpdateStatement {
  std::string table_name;
  std::string column_name;
  Literal value;
  std::optional<Expression> where;
  SourceLocation location;

  [[nodiscard]] friend bool operator==(
      const UpdateStatement&, const UpdateStatement&) = default;
};

using Statement =
    std::variant<CreateDatabaseStatement, UseDatabaseStatement,
                 CreateTableStatement, InsertStatement, SelectStatement,
                 DeleteStatement, UpdateStatement, CreateIndexStatement,
                 ExplainStatement>;

}  // namespace curiodb::sql
