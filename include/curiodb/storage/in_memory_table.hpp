#pragma once

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/storage/row.hpp"
#include "curiodb/storage/row_validation.hpp"

namespace curiodb::storage {

class InMemoryTable {
 public:
  explicit InMemoryTable(catalog::TableSchema schema);

  [[nodiscard]] const catalog::TableSchema& schema() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t row_count() const noexcept;
  [[nodiscard]] std::span<const Row> rows() const noexcept;

  [[nodiscard]] RowValidationResult insert(Row row);

 private:
  catalog::TableSchema schema_;
  std::vector<Row> rows_;
};

}  // namespace curiodb::storage

