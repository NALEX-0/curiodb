#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "curiodb/storage/row.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::storage {

enum class SerializationErrorCode {
  InputTooLarge,
  UnexpectedEnd,
  InvalidTypeTag,
  TrailingData,
};

struct SerializationError {
  SerializationErrorCode code;
  std::string message;
  std::size_t offset;

  [[nodiscard]] friend bool operator==(
      const SerializationError&, const SerializationError&) = default;
};

using SerializedBytes = std::vector<std::byte>;
using SerializationResult = std::variant<SerializedBytes, SerializationError>;
using ValueDeserializationResult = std::variant<Value, SerializationError>;
using RowDeserializationResult = std::variant<Row, SerializationError>;

[[nodiscard]] SerializationResult serialize_value(const Value& value);
[[nodiscard]] ValueDeserializationResult deserialize_value(
    std::span<const std::byte> bytes);
[[nodiscard]] SerializationResult serialize_row(const Row& row);
[[nodiscard]] RowDeserializationResult deserialize_row(
    std::span<const std::byte> bytes);

}  // namespace curiodb::storage

