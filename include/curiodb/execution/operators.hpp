#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "curiodb/storage/in_memory_table.hpp"
#include "curiodb/storage/row.hpp"

namespace curiodb::execution {

using RowReference = std::reference_wrapper<const storage::Row>;
using RowSet = std::vector<RowReference>;

class SequentialScanOperator {
 public:
  explicit SequentialScanOperator(const storage::InMemoryTable& table);

  [[nodiscard]] RowSet execute() const;

 private:
  const storage::InMemoryTable& table_;
};

class FilterOperator {
 public:
  using Predicate = std::function<bool(const storage::Row&)>;

  FilterOperator(RowSet input, Predicate predicate);

  [[nodiscard]] RowSet execute() const;

 private:
  RowSet input_;
  Predicate predicate_;
};

class ProjectionOperator {
 public:
  ProjectionOperator(RowSet input, std::vector<std::size_t> column_indexes);

  [[nodiscard]] std::vector<std::vector<std::string>> execute() const;

 private:
  RowSet input_;
  std::vector<std::size_t> column_indexes_;
};

}  // namespace curiodb::execution

