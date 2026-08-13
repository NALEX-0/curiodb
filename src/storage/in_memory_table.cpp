#include "curiodb/storage/in_memory_table.hpp"

#include <algorithm>
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
  for (std::size_t column_index = 0; column_index < schema_.columns.size();
       ++column_index) {
    const auto& column = schema_.columns[column_index];
    if (!(column.primary_key || column.unique)) {
      continue;
    }
    if (row[column_index].is_null()) {
      continue;
    }
    if (std::any_of(rows_.begin(), rows_.end(), [&](const Row& existing) {
          return existing[column_index] == row[column_index];
        })) {
      return RowValidationError{
          .code = RowValidationErrorCode::TypeMismatch,
          .message = "duplicate value for " +
                     std::string{column.primary_key ? "PRIMARY KEY '"
                                                    : "UNIQUE column '"} +
                     column.name + "'"};
    }
  }
  rows_.push_back(std::move(row));
  return std::nullopt;
}

std::size_t InMemoryTable::delete_where(
    const std::function<bool(const Row&)>& predicate) {
  const std::size_t original_size = rows_.size();
  std::erase_if(rows_, predicate);
  return original_size - rows_.size();
}

std::size_t InMemoryTable::update_where(
    const std::function<bool(const Row&)>& predicate,
    std::size_t column_index, const Value& value) {
  std::size_t count = 0;
  for (auto& row : rows_) {
    if (predicate(row)) {
      row.set(column_index, value);
      ++count;
    }
  }
  return count;
}

}  // namespace curiodb::storage
