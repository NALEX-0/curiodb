#include "curiodb/execution/operators.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace curiodb::execution {

SequentialScanOperator::SequentialScanOperator(
    const storage::InMemoryTable& table)
    : table_(table) {}

RowSet SequentialScanOperator::execute() const {
  RowSet rows;
  rows.reserve(table_.row_count());
  for (const auto& row : table_.rows()) {
    rows.emplace_back(std::cref(row));
  }
  return rows;
}

FilterOperator::FilterOperator(RowSet input, Predicate predicate)
    : input_(std::move(input)), predicate_(std::move(predicate)) {}

RowSet FilterOperator::execute() const {
  RowSet rows;
  rows.reserve(input_.size());
  for (const RowReference row : input_) {
    if (predicate_(row.get())) {
      rows.push_back(row);
    }
  }
  return rows;
}

ProjectionOperator::ProjectionOperator(
    RowSet input, std::vector<std::size_t> column_indexes)
    : input_(std::move(input)), column_indexes_(std::move(column_indexes)) {}

std::vector<std::vector<std::string>> ProjectionOperator::execute() const {
  std::vector<std::vector<std::string>> projected_rows;
  projected_rows.reserve(input_.size());
  for (const RowReference row : input_) {
    std::vector<std::string> values;
    values.reserve(column_indexes_.size());
    for (const std::size_t index : column_indexes_) {
      values.push_back(row.get()[index].to_string());
    }
    projected_rows.push_back(std::move(values));
  }
  return projected_rows;
}

}  // namespace curiodb::execution

