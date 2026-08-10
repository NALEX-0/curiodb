#include "curiodb/types/data_type.hpp"

#include <string>

namespace curiodb {

std::string format_data_type(const DataType& type) {
  switch (type.kind) {
    case DataTypeKind::Integer:
      return "INT";
    case DataTypeKind::Double:
      return "DOUBLE";
    case DataTypeKind::Varchar:
      return "VARCHAR(" + std::to_string(type.length.value_or(0)) + ")";
  }
  return "UNKNOWN";
}

}  // namespace curiodb

