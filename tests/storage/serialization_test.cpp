#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "curiodb/storage/row.hpp"
#include "curiodb/storage/serialization.hpp"
#include "curiodb/types/data_type.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::storage {
namespace {

SerializedBytes expect_bytes(SerializationResult result) {
  EXPECT_TRUE(std::holds_alternative<SerializedBytes>(result));
  return std::get<SerializedBytes>(std::move(result));
}

TEST(SerializationTest, UsesStableLittleEndianIntegerEncoding) {
  const auto bytes = expect_bytes(serialize_value(Value{std::int64_t{42}}));

  EXPECT_EQ(bytes,
            (SerializedBytes{std::byte{1}, std::byte{42}, std::byte{0},
                             std::byte{0}, std::byte{0}, std::byte{0},
                             std::byte{0}, std::byte{0}, std::byte{0}}));
}

TEST(SerializationTest, RoundTripsEveryValueType) {
  const std::vector<Value> values{Value{}, Value{std::int64_t{-42}}, Value{-0.0},
                                  Value{std::string{"O'Brien"}}};

  for (const auto& original : values) {
    const auto bytes = expect_bytes(serialize_value(original));
    const auto decoded = deserialize_value(bytes);
    ASSERT_TRUE(std::holds_alternative<Value>(decoded));
    const Value& value = std::get<Value>(decoded);
    EXPECT_EQ(value.is_null(), original.is_null());
    if (value.is_null()) {
      EXPECT_EQ(value, original);
    } else if (value.type() == DataTypeKind::Double) {
      EXPECT_EQ(std::bit_cast<std::uint64_t>(value.as_double()),
                std::bit_cast<std::uint64_t>(original.as_double()));
    } else {
      EXPECT_EQ(value, original);
    }
  }
}

TEST(SerializationTest, UsesSingleByteNullEncoding) {
  EXPECT_EQ(expect_bytes(serialize_value(Value{})),
            (SerializedBytes{std::byte{0}}));
}

TEST(SerializationTest, RoundTripsCompleteRow) {
  const Row original{Value{std::int64_t{1}}, Value{"Alice"}, Value{65000.5}};
  const auto bytes = expect_bytes(serialize_row(original));

  const auto decoded = deserialize_row(bytes);

  ASSERT_TRUE(std::holds_alternative<Row>(decoded));
  EXPECT_EQ(std::get<Row>(decoded), original);
}

TEST(SerializationTest, RoundTripsEmptyAndUtf8Rows) {
  const auto empty = deserialize_row(expect_bytes(serialize_row(Row{})));
  ASSERT_TRUE(std::holds_alternative<Row>(empty));
  EXPECT_TRUE(std::get<Row>(empty).empty());

  const Row utf8{Value{std::string{"Αθήνα"}}};
  const auto decoded = deserialize_row(expect_bytes(serialize_row(utf8)));
  ASSERT_TRUE(std::holds_alternative<Row>(decoded));
  EXPECT_EQ(std::get<Row>(decoded), utf8);
}

TEST(SerializationTest, RejectsTruncatedDataAtEveryBoundary) {
  const auto complete = expect_bytes(serialize_row(
      Row{Value{std::int64_t{1}}, Value{"Alice"}, Value{65000.5}}));

  for (std::size_t size = 0; size < complete.size(); ++size) {
    const auto result =
        deserialize_row(std::span<const std::byte>{complete.data(), size});
    ASSERT_TRUE(std::holds_alternative<SerializationError>(result));
    EXPECT_EQ(std::get<SerializationError>(result).code,
              SerializationErrorCode::UnexpectedEnd);
  }
}

TEST(SerializationTest, RejectsUnknownTypeTag) {
  const SerializedBytes bytes{std::byte{99}};
  const auto result = deserialize_value(bytes);

  ASSERT_TRUE(std::holds_alternative<SerializationError>(result));
  const auto& error = std::get<SerializationError>(result);
  EXPECT_EQ(error.code, SerializationErrorCode::InvalidTypeTag);
  EXPECT_EQ(error.offset, 0U);
}

TEST(SerializationTest, RejectsTrailingData) {
  auto bytes = expect_bytes(serialize_value(Value{std::int64_t{1}}));
  bytes.push_back(std::byte{0});
  const auto result = deserialize_value(bytes);

  ASSERT_TRUE(std::holds_alternative<SerializationError>(result));
  EXPECT_EQ(std::get<SerializationError>(result).code,
            SerializationErrorCode::TrailingData);
}

TEST(SerializationTest, RejectsImpossibleRowCountBeforeAllocating) {
  const SerializedBytes bytes{std::byte{255}, std::byte{255}, std::byte{255},
                              std::byte{127}};
  const auto result = deserialize_row(bytes);

  ASSERT_TRUE(std::holds_alternative<SerializationError>(result));
  EXPECT_EQ(std::get<SerializationError>(result).code,
            SerializationErrorCode::UnexpectedEnd);
}

}  // namespace
}  // namespace curiodb::storage
