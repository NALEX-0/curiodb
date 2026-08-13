#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/storage/row.hpp"

namespace curiodb::storage {

enum class RowValidationErrorCode {
  ValueCountMismatch,
  TypeMismatch,
  VarcharTooLong,
  InvalidSchema,
  NullNotAllowed,
};

struct RowValidationError {
  RowValidationErrorCode code;
  std::string message;
  std::optional<std::size_t> column_index;

  [[nodiscard]] friend bool operator==(
      const RowValidationError&, const RowValidationError&) = default;
};

using RowValidationResult = std::optional<RowValidationError>;

[[nodiscard]] RowValidationResult validate_row(
    const catalog::TableSchema& schema, const Row& row);

}  // namespace curiodb::storage
