#include "curiodb/storage/serialization.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "curiodb/storage/row.hpp"
#include "curiodb/types/data_type.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::storage {
namespace {

constexpr std::uint8_t kIntegerTag = 1;
constexpr std::uint8_t kDoubleTag = 2;
constexpr std::uint8_t kVarcharTag = 3;

static_assert(sizeof(double) == sizeof(std::uint64_t));
static_assert(std::numeric_limits<double>::is_iec559);

void append_u8(SerializedBytes& bytes, std::uint8_t value) {
  bytes.push_back(static_cast<std::byte>(value));
}

void append_u32(SerializedBytes& bytes, std::uint32_t value) {
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    append_u8(bytes, static_cast<std::uint8_t>(value >> shift));
  }
}

void append_u64(SerializedBytes& bytes, std::uint64_t value) {
  for (unsigned int shift = 0; shift < 64; shift += 8) {
    append_u8(bytes, static_cast<std::uint8_t>(value >> shift));
  }
}

SerializationError too_large(std::string message) {
  return {SerializationErrorCode::InputTooLarge, std::move(message), 0};
}

bool append_value(SerializedBytes& bytes, const Value& value) {
  switch (value.type()) {
    case DataTypeKind::Integer:
      append_u8(bytes, kIntegerTag);
      append_u64(bytes, std::bit_cast<std::uint64_t>(value.as_integer()));
      return true;
    case DataTypeKind::Double:
      append_u8(bytes, kDoubleTag);
      append_u64(bytes, std::bit_cast<std::uint64_t>(value.as_double()));
      return true;
    case DataTypeKind::Varchar: {
      const auto& string = value.as_string();
      if (string.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
      }
      append_u8(bytes, kVarcharTag);
      append_u32(bytes, static_cast<std::uint32_t>(string.size()));
      for (const char character : string) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
      }
      return true;
    }
  }
  return false;
}

class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - offset_;
  }

  bool read_u8(std::uint8_t& value) {
    if (remaining() < 1) {
      return false;
    }
    value = std::to_integer<std::uint8_t>(bytes_[offset_++]);
    return true;
  }

  bool read_u32(std::uint32_t& value) {
    if (remaining() < 4) {
      return false;
    }
    value = 0;
    for (unsigned int shift = 0; shift < 32; shift += 8) {
      value |= static_cast<std::uint32_t>(
                   std::to_integer<std::uint8_t>(bytes_[offset_++]))
               << shift;
    }
    return true;
  }

  bool read_u64(std::uint64_t& value) {
    if (remaining() < 8) {
      return false;
    }
    value = 0;
    for (unsigned int shift = 0; shift < 64; shift += 8) {
      value |= static_cast<std::uint64_t>(
                   std::to_integer<std::uint8_t>(bytes_[offset_++]))
               << shift;
    }
    return true;
  }

  bool read_string(std::size_t length, std::string& value) {
    if (remaining() < length) {
      return false;
    }
    value.clear();
    value.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
      value.push_back(static_cast<char>(
          std::to_integer<unsigned char>(bytes_[offset_++])));
    }
    return true;
  }

 private:
  std::span<const std::byte> bytes_;
  std::size_t offset_{0};
};

SerializationError unexpected_end(std::size_t offset) {
  return {SerializationErrorCode::UnexpectedEnd,
          "unexpected end of serialized data", offset};
}

ValueDeserializationResult read_value(Reader& reader) {
  const std::size_t tag_offset = reader.offset();
  std::uint8_t tag = 0;
  if (!reader.read_u8(tag)) {
    return unexpected_end(reader.offset());
  }
  if (tag == kIntegerTag) {
    std::uint64_t bits = 0;
    if (!reader.read_u64(bits)) {
      return unexpected_end(reader.offset());
    }
    return Value{std::bit_cast<std::int64_t>(bits)};
  }
  if (tag == kDoubleTag) {
    std::uint64_t bits = 0;
    if (!reader.read_u64(bits)) {
      return unexpected_end(reader.offset());
    }
    return Value{std::bit_cast<double>(bits)};
  }
  if (tag == kVarcharTag) {
    std::uint32_t length = 0;
    if (!reader.read_u32(length)) {
      return unexpected_end(reader.offset());
    }
    std::string value;
    if (!reader.read_string(length, value)) {
      return unexpected_end(reader.offset());
    }
    return Value{std::move(value)};
  }
  return SerializationError{SerializationErrorCode::InvalidTypeTag,
                            "invalid value type tag", tag_offset};
}

SerializationError trailing_data(std::size_t offset) {
  return {SerializationErrorCode::TrailingData,
          "trailing bytes after serialized value", offset};
}

}  // namespace

SerializationResult serialize_value(const Value& value) {
  SerializedBytes bytes;
  if (!append_value(bytes, value)) {
    return too_large("VARCHAR value exceeds serialization limit");
  }
  return bytes;
}

ValueDeserializationResult deserialize_value(
    std::span<const std::byte> bytes) {
  Reader reader{bytes};
  auto result = read_value(reader);
  if (std::holds_alternative<SerializationError>(result)) {
    return result;
  }
  if (reader.remaining() != 0) {
    return trailing_data(reader.offset());
  }
  return result;
}

SerializationResult serialize_row(const Row& row) {
  if (row.size() > std::numeric_limits<std::uint32_t>::max()) {
    return too_large("row contains too many values");
  }
  SerializedBytes bytes;
  append_u32(bytes, static_cast<std::uint32_t>(row.size()));
  for (const auto& value : row.values()) {
    if (!append_value(bytes, value)) {
      return too_large("VARCHAR value exceeds serialization limit");
    }
  }
  return bytes;
}

RowDeserializationResult deserialize_row(std::span<const std::byte> bytes) {
  Reader reader{bytes};
  std::uint32_t count = 0;
  if (!reader.read_u32(count)) {
    return unexpected_end(reader.offset());
  }
  if (count > reader.remaining()) {
    return unexpected_end(reader.offset());
  }

  std::vector<Value> values;
  values.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    auto value = read_value(reader);
    if (const auto* error = std::get_if<SerializationError>(&value)) {
      return *error;
    }
    values.push_back(std::get<Value>(std::move(value)));
  }
  if (reader.remaining() != 0) {
    return trailing_data(reader.offset());
  }
  return Row{std::move(values)};
}

}  // namespace curiodb::storage
