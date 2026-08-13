#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/catalog/catalog_storage.hpp"
#include "curiodb/execution/operators.hpp"
#include "curiodb/sql/ast.hpp"

namespace curiodb::execution {

struct SequentialScanPlan {
  std::string table_name;
};

struct IndexScanPlan {
  std::string table_name;
  std::string index_name;
  std::string column_name;
  std::int64_t key;
};

struct PlanNode;

struct FilterPlan {
  FilterOperator::Predicate predicate;
  std::string description;
  std::shared_ptr<PlanNode> child;
};

struct ProjectionPlan {
  std::vector<std::size_t> column_indexes;
  std::vector<std::string> column_names;
  std::shared_ptr<PlanNode> child;
};

struct PlanNode {
  using Node =
      std::variant<SequentialScanPlan, IndexScanPlan, FilterPlan, ProjectionPlan>;
  Node node;
};

struct PlannerError {
  std::string message;
};

using PlanResult = std::variant<PlanNode, PlannerError>;

[[nodiscard]] PlanResult plan_select(
    const sql::SelectStatement& statement,
    const catalog::TableSchema& schema,
    const std::vector<catalog::StoredIndex>& indexes = {});
[[nodiscard]] std::string format_plan(const PlanNode& plan);
[[nodiscard]] std::vector<std::vector<std::string>> execute_plan(
    const PlanNode& plan, RowSet rows);

}  // namespace curiodb::execution
