#include "curiodb/execution/planner.hpp"

#include <algorithm>
#include <cctype>
#include <compare>
#include <cstddef>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/execution/operators.hpp"
#include "curiodb/sql/ast.hpp"
#include "curiodb/types/data_type.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::execution {
namespace {

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

std::string comparison_name(sql::ComparisonOperator operation) {
  switch (operation) {
    case sql::ComparisonOperator::Equal:
      return "=";
    case sql::ComparisonOperator::NotEqual:
      return "!=";
    case sql::ComparisonOperator::LessThan:
      return "<";
    case sql::ComparisonOperator::LessThanOrEqual:
      return "<=";
    case sql::ComparisonOperator::GreaterThan:
      return ">";
    case sql::ComparisonOperator::GreaterThanOrEqual:
      return ">=";
  }
  return "?";
}

struct BoundPredicate {
  FilterOperator::Predicate predicate;
  std::string description;
};

std::variant<BoundPredicate, PlannerError> bind_expression(
    const sql::Expression& expression, const catalog::TableSchema& schema) {
  if (const auto* comparison =
          std::get_if<sql::ComparisonExpression>(&expression.node)) {
    const std::string name = normalize(comparison->column_name);
    const auto column = std::find_if(
        schema.columns.begin(), schema.columns.end(),
        [&name](const catalog::ColumnSchema& candidate) {
          return normalize(candidate.name) == name;
        });
    if (column == schema.columns.end()) {
      return PlannerError{.message = "column '" + comparison->column_name +
                                     "' does not exist"};
    }
    const auto index = static_cast<std::size_t>(
        std::distance(schema.columns.begin(), column));
    const Value value = value_from_literal(comparison->value);
    if (value.type() != column->type.kind) {
      return PlannerError{
          .message = "column '" + column->name + "': expected " +
                     format_data_type(column->type) + ", received " +
                     value_type_name(value.type())};
    }
    const auto operation = comparison->operation;
    return BoundPredicate{
        .predicate = [index, operation, value](const storage::Row& row) {
          return matches(row[index], operation, value);
        },
        .description = column->name + " " + comparison_name(operation) +
                       " " + value.to_string(),
    };
  }

  const auto& logical =
      std::get<std::shared_ptr<sql::LogicalExpression>>(expression.node);
  if (logical == nullptr) {
    return PlannerError{.message = "invalid WHERE expression"};
  }
  auto left_result = bind_expression(logical->left, schema);
  if (const auto* error = std::get_if<PlannerError>(&left_result)) {
    return *error;
  }
  auto right_result = bind_expression(logical->right, schema);
  if (const auto* error = std::get_if<PlannerError>(&right_result)) {
    return *error;
  }
  auto left = std::get<BoundPredicate>(std::move(left_result));
  auto right = std::get<BoundPredicate>(std::move(right_result));
  const bool is_and = logical->operation == sql::LogicalOperator::And;
  return BoundPredicate{
      .predicate = [left_predicate = std::move(left.predicate),
                    right_predicate = std::move(right.predicate),
                    is_and](const storage::Row& row) {
        return is_and ? left_predicate(row) && right_predicate(row)
                      : left_predicate(row) || right_predicate(row);
      },
      .description = "(" + left.description + (is_and ? " AND " : " OR ") +
                     right.description + ")",
  };
}

void format_node(const PlanNode& plan, std::size_t depth,
                 std::ostringstream& output) {
  output << std::string(depth * 2, ' ');
  std::visit(
      [&](const auto& node) {
        using NodeType = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<NodeType, SequentialScanPlan>) {
          output << "SequentialScan(table=" << node.table_name << ")\n";
        } else if constexpr (std::is_same_v<NodeType, IndexScanPlan>) {
          output << "IndexScan(index=" << node.index_name
                 << ", condition=" << node.column_name << " = " << node.key
                 << ")\n";
        } else if constexpr (std::is_same_v<NodeType, FilterPlan>) {
          output << "Filter(condition=" << node.description << ")\n";
          format_node(*node.child, depth + 1, output);
        } else {
          output << "Projection(columns=";
          for (std::size_t index = 0; index < node.column_names.size(); ++index) {
            output << (index == 0 ? "" : ", ") << node.column_names[index];
          }
          output << ")\n";
          format_node(*node.child, depth + 1, output);
        }
      },
      plan.node);
}

RowSet execute_rows(const PlanNode& plan, RowSet rows) {
  if (std::holds_alternative<SequentialScanPlan>(plan.node) ||
      std::holds_alternative<IndexScanPlan>(plan.node)) {
    return rows;
  }
  const auto& filter = std::get<FilterPlan>(plan.node);
  rows = execute_rows(*filter.child, std::move(rows));
  return FilterOperator{std::move(rows), filter.predicate}.execute();
}

}  // namespace

PlanResult plan_select(const sql::SelectStatement& statement,
                       const catalog::TableSchema& schema,
                       const std::vector<catalog::StoredIndex>& indexes) {
  std::shared_ptr<PlanNode> current;
  if (statement.where.has_value()) {
    const auto* comparison =
        std::get_if<sql::ComparisonExpression>(&statement.where->node);
    const auto index = comparison == nullptr
                           ? indexes.end()
                           : std::find_if(indexes.begin(), indexes.end(),
                                          [comparison](const auto& candidate) {
                                            return normalize(candidate.column_name) ==
                                                       normalize(comparison->column_name) &&
                                                   comparison->operation ==
                                                       sql::ComparisonOperator::Equal &&
                                                   std::holds_alternative<std::int64_t>(
                                                       comparison->value.value);
                                          });
    if (index != indexes.end()) {
      current = std::make_shared<PlanNode>(PlanNode{IndexScanPlan{
          .table_name = schema.name,
          .index_name = index->name,
          .column_name = index->column_name,
          .key = std::get<std::int64_t>(comparison->value.value)}});
    }
  }
  if (current == nullptr) {
    current = std::make_shared<PlanNode>(
        PlanNode{SequentialScanPlan{.table_name = schema.name}});
  }
  if (statement.where.has_value()) {
    auto bound_result = bind_expression(*statement.where, schema);
    if (const auto* error = std::get_if<PlannerError>(&bound_result)) {
      return *error;
    }
    auto bound = std::get<BoundPredicate>(std::move(bound_result));
    current = std::make_shared<PlanNode>(PlanNode{FilterPlan{
        .predicate = std::move(bound.predicate),
        .description = std::move(bound.description),
        .child = std::move(current),
    }});
  }

  std::vector<std::size_t> column_indexes;
  std::vector<std::string> names;
  if (statement.columns.empty()) {
    column_indexes.reserve(schema.columns.size());
    names.reserve(schema.columns.size());
    for (std::size_t index = 0; index < schema.columns.size(); ++index) {
      column_indexes.push_back(index);
      names.push_back(schema.columns[index].name);
    }
  } else {
    std::unordered_set<std::size_t> selected;
    for (const auto& requested : statement.columns) {
      const std::string name = normalize(requested.name);
      const auto column = std::find_if(
          schema.columns.begin(), schema.columns.end(),
          [&name](const catalog::ColumnSchema& candidate) {
            return normalize(candidate.name) == name;
          });
      if (column == schema.columns.end()) {
        return PlannerError{.message = "column '" + requested.name +
                                       "' does not exist"};
      }
      const auto index = static_cast<std::size_t>(
          std::distance(schema.columns.begin(), column));
      if (!selected.insert(index).second) {
        return PlannerError{.message = "column '" + requested.name +
                                       "' selected more than once"};
      }
      column_indexes.push_back(index);
      names.push_back(column->name);
    }
  }
  return PlanNode{ProjectionPlan{.column_indexes = std::move(column_indexes),
                                 .column_names = std::move(names),
                                 .child = std::move(current)}};
}

std::string format_plan(const PlanNode& plan) {
  std::ostringstream output;
  format_node(plan, 0, output);
  std::string result = output.str();
  if (!result.empty()) {
    result.pop_back();
  }
  return result;
}

std::vector<std::vector<std::string>> execute_plan(const PlanNode& plan,
                                                   RowSet rows) {
  const auto* projection = std::get_if<ProjectionPlan>(&plan.node);
  if (projection == nullptr) {
    return {};
  }
  rows = execute_rows(*projection->child, std::move(rows));
  return ProjectionOperator{std::move(rows), projection->column_indexes}
      .execute();
}

}  // namespace curiodb::execution
