#pragma once

#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

#include "curiodb/types/value.hpp"

namespace curiodb::storage {

class Row {
 public:
  Row() = default;
  explicit Row(std::vector<Value> values) : values_(std::move(values)) {}
  Row(std::initializer_list<Value> values) : values_(values) {}

  [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
  [[nodiscard]] const Value& at(std::size_t index) const {
    return values_.at(index);
  }
  [[nodiscard]] const Value& operator[](std::size_t index) const noexcept {
    return values_[index];
  }
  [[nodiscard]] const std::vector<Value>& values() const noexcept {
    return values_;
  }

  [[nodiscard]] friend bool operator==(const Row&, const Row&) = default;

 private:
  std::vector<Value> values_;
};

}  // namespace curiodb::storage

