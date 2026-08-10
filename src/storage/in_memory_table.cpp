#include "curiodb/storage/in_memory_table.hpp"

#include <cstddef>
#include <span>
#include <utility>

namespace curiodb::storage {

InMemoryTable::InMemoryTable(catalog::TableSchema schema)
    : schema_(std::move(schema)) {}

const catalog::TableSchema& InMemoryTable::schema() const noexcept {
  return schema_;
}

bool InMemoryTable::empty() const noexcept { return rows_.empty(); }

std::size_t InMemoryTable::row_count() const noexcept { return rows_.size(); }

std::span<const Row> InMemoryTable::rows() const noexcept { return rows_; }

RowValidationResult InMemoryTable::insert(Row row) {
  if (auto validation_error = validate_row(schema_, row);
      validation_error.has_value()) {
    return validation_error;
  }
  rows_.push_back(std::move(row));
  return std::nullopt;
}

}  // namespace curiodb::storage

