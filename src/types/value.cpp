#include "curiodb/types/value.hpp"

#include <charconv>
#include <compare>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace curiodb {

Value::Value(std::int64_t value) noexcept : storage_(value) {}

Value::Value(double value) noexcept : storage_(value) {}

Value::Value(std::string value) : storage_(std::move(value)) {}

Value::Value(const char* value) : storage_(std::string{value}) {}

DataTypeKind Value::type() const noexcept {
  switch (storage_.index()) {
    case 0:
      return DataTypeKind::Integer;
    case 1:
      return DataTypeKind::Double;
    case 2:
      return DataTypeKind::Varchar;
    default:
      return DataTypeKind::Integer;
  }
}

std::int64_t Value::as_integer() const {
  return std::get<std::int64_t>(storage_);
}

double Value::as_double() const { return std::get<double>(storage_); }

const std::string& Value::as_string() const {
  return std::get<std::string>(storage_);
}

std::string Value::to_string() const {
  if (const auto* integer = std::get_if<std::int64_t>(&storage_)) {
    return std::to_string(*integer);
  }
  if (const auto* floating_point = std::get_if<double>(&storage_)) {
    char buffer[64];
    const auto conversion =
        std::to_chars(buffer, buffer + sizeof(buffer), *floating_point,
                      std::chars_format::general,
                      std::numeric_limits<double>::max_digits10);
    if (conversion.ec == std::errc{}) {
      return std::string{buffer, conversion.ptr};
    }
    return "<unprintable double>";
  }
  return std::get<std::string>(storage_);
}

std::partial_ordering operator<=>(const Value& left, const Value& right) {
  if (left.storage_.index() != right.storage_.index()) {
    return std::partial_ordering::unordered;
  }
  if (const auto* integer = std::get_if<std::int64_t>(&left.storage_)) {
    return *integer <=> std::get<std::int64_t>(right.storage_);
  }
  if (const auto* floating_point = std::get_if<double>(&left.storage_)) {
    return *floating_point <=> std::get<double>(right.storage_);
  }
  return std::get<std::string>(left.storage_) <=>
         std::get<std::string>(right.storage_);
}

}  // namespace curiodb
