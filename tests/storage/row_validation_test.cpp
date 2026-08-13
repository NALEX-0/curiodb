#include <cstdint>

#include <gtest/gtest.h>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/storage/row.hpp"
#include "curiodb/storage/row_validation.hpp"
#include "curiodb/types/data_type.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::storage {
namespace {

catalog::TableSchema employee_schema() {
  return {
      .name = "employees",
      .columns =
          {
              {.name = "id", .type = {.kind = DataTypeKind::Integer}},
              {.name = "name",
               .type = {.kind = DataTypeKind::Varchar, .length = 10}},
              {.name = "salary", .type = {.kind = DataTypeKind::Double}},
          },
  };
}

TEST(RowValidationTest, AcceptsRowMatchingSchema) {
  const Row row{Value{std::int64_t{1}}, Value{"Alice"}, Value{65000.0}};

  EXPECT_FALSE(validate_row(employee_schema(), row).has_value());
}

TEST(RowValidationTest, AcceptsNullForNullableColumn) {
  auto schema = employee_schema();
  const Row row{Value{std::int64_t{1}}, Value{}, Value{65000.0}};

  EXPECT_FALSE(validate_row(schema, row).has_value());
}

TEST(RowValidationTest, RejectsNullForNotNullAndPrimaryKeyColumns) {
  auto not_null_schema = employee_schema();
  not_null_schema.columns[1].not_null = true;
  const auto not_null = validate_row(
      not_null_schema,
      Row{Value{std::int64_t{1}}, Value{}, Value{65000.0}});
  ASSERT_TRUE(not_null.has_value());
  EXPECT_EQ(not_null->code, RowValidationErrorCode::NullNotAllowed);

  auto primary_key_schema = employee_schema();
  primary_key_schema.columns[0].primary_key = true;
  const auto primary_key = validate_row(
      primary_key_schema, Row{Value{}, Value{"Alice"}, Value{65000.0}});
  ASSERT_TRUE(primary_key.has_value());
  EXPECT_EQ(primary_key->code, RowValidationErrorCode::NullNotAllowed);
}

TEST(RowValidationTest, RejectsWrongNumberOfValues) {
  const Row row{Value{std::int64_t{1}}, Value{"Alice"}};
  const auto result = validate_row(employee_schema(), row);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, RowValidationErrorCode::ValueCountMismatch);
  EXPECT_EQ(result->message, "expected 3 values, received 2");
  EXPECT_FALSE(result->column_index.has_value());
}

TEST(RowValidationTest, RejectsValueWithWrongType) {
  const Row row{Value{std::int64_t{1}}, Value{2.5}, Value{65000.0}};
  const auto result = validate_row(employee_schema(), row);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, RowValidationErrorCode::TypeMismatch);
  EXPECT_EQ(result->message,
            "column 'name': expected VARCHAR(10), received DOUBLE");
  EXPECT_EQ(result->column_index, 1U);
}

TEST(RowValidationTest, RejectsVarcharExceedingByteLimit) {
  const Row row{Value{std::int64_t{1}}, Value{"Christopher"}, Value{65000.0}};
  const auto result = validate_row(employee_schema(), row);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, RowValidationErrorCode::VarcharTooLong);
  EXPECT_EQ(result->message,
            "column 'name': VARCHAR value has 11 bytes, maximum is 10");
  EXPECT_EQ(result->column_index, 1U);
}

TEST(RowValidationTest, AcceptsVarcharExactlyAtLimit) {
  const Row row{Value{std::int64_t{1}}, Value{"0123456789"}, Value{65000.0}};

  EXPECT_FALSE(validate_row(employee_schema(), row).has_value());
}

TEST(RowValidationTest, RejectsMalformedVarcharSchema) {
  auto schema = employee_schema();
  schema.columns[1].type.length.reset();
  const Row row{Value{std::int64_t{1}}, Value{"Alice"}, Value{65000.0}};
  const auto result = validate_row(schema, row);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, RowValidationErrorCode::InvalidSchema);
  EXPECT_EQ(result->column_index, 1U);
}

TEST(RowValidationTest, RejectsLengthOnNonVarcharSchema) {
  auto schema = employee_schema();
  schema.columns[0].type.length = 4;
  const Row row{Value{std::int64_t{1}}, Value{"Alice"}, Value{65000.0}};
  const auto result = validate_row(schema, row);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->code, RowValidationErrorCode::InvalidSchema);
  EXPECT_EQ(result->column_index, 0U);
}

}  // namespace
}  // namespace curiodb::storage
