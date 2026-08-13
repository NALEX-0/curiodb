#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <variant>

#include "curiodb/types/data_type.hpp"

namespace curiodb {

class Value {
 public:
  Value() noexcept = default;
  explicit Value(std::int64_t value) noexcept;
  explicit Value(double value) noexcept;
  explicit Value(std::string value);
  explicit Value(const char* value);

  [[nodiscard]] bool is_null() const noexcept;
  [[nodiscard]] DataTypeKind type() const noexcept;
  [[nodiscard]] std::int64_t as_integer() const;
  [[nodiscard]] double as_double() const;
  [[nodiscard]] const std::string& as_string() const;
  [[nodiscard]] std::string to_string() const;

  [[nodiscard]] friend bool operator==(const Value&, const Value&) = default;
  friend std::partial_ordering operator<=>(const Value& left,
                                            const Value& right);

 private:
  using Storage =
      std::variant<std::monostate, std::int64_t, double, std::string>;
  Storage storage_;
};

}  // namespace curiodb
