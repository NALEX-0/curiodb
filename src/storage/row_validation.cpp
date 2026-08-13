#include "curiodb/storage/row_validation.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "curiodb/types/data_type.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::storage {
namespace {

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

RowValidationError column_error(RowValidationErrorCode code,
                                const catalog::ColumnSchema& column,
                                std::size_t column_index,
                                std::string detail) {
  return {
      .code = code,
      .message = "column '" + column.name + "': " + std::move(detail),
      .column_index = column_index,
  };
}

}  // namespace

RowValidationResult validate_row(const catalog::TableSchema& schema,
                                 const Row& row) {
  if (row.size() != schema.columns.size()) {
    return RowValidationError{
        .code = RowValidationErrorCode::ValueCountMismatch,
        .message = "expected " + std::to_string(schema.columns.size()) +
                   " values, received " + std::to_string(row.size()),
        .column_index = std::nullopt,
    };
  }

  for (std::size_t index = 0; index < schema.columns.size(); ++index) {
    const auto& column = schema.columns[index];
    const auto& value = row[index];

    const bool varchar = column.type.kind == DataTypeKind::Varchar;
    if ((varchar && (!column.type.length.has_value() || *column.type.length == 0)) ||
        (!varchar && column.type.length.has_value())) {
      return column_error(RowValidationErrorCode::InvalidSchema, column, index,
                          "invalid type definition");
    }
    if (value.is_null()) {
      if (column.not_null || column.primary_key) {
        return column_error(RowValidationErrorCode::NullNotAllowed, column,
                            index, "NULL is not allowed");
      }
      continue;
    }
    if (value.type() != column.type.kind) {
      return column_error(
          RowValidationErrorCode::TypeMismatch, column, index,
          "expected " + format_data_type(column.type) + ", received " +
              value_type_name(value.type()));
    }
    if (varchar && value.as_string().size() > *column.type.length) {
      return column_error(
          RowValidationErrorCode::VarcharTooLong, column, index,
          "VARCHAR value has " + std::to_string(value.as_string().size()) +
              " bytes, maximum is " + std::to_string(*column.type.length));
    }
  }

  return std::nullopt;
}

}  // namespace curiodb::storage
