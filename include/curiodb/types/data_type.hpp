#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace curiodb {

enum class DataTypeKind {
  Integer,
  Double,
  Varchar,
};

struct DataType {
  DataTypeKind kind{DataTypeKind::Integer};
  std::optional<std::size_t> length;

  [[nodiscard]] friend bool operator==(const DataType&, const DataType&) =
      default;
};

[[nodiscard]] std::string format_data_type(const DataType& type);

}  // namespace curiodb
