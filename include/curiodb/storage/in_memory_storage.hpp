#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/storage/in_memory_table.hpp"

namespace curiodb::storage {

class InMemoryStorage {
 public:
  void create_table(std::string_view database,
                    const catalog::TableSchema& schema);

  [[nodiscard]] InMemoryTable* find_table(std::string_view database,
                                           std::string_view table);
  [[nodiscard]] const InMemoryTable* find_table(
      std::string_view database, std::string_view table) const;

 private:
  using Tables = std::unordered_map<std::string, InMemoryTable>;
  std::unordered_map<std::string, Tables> databases_;
};

}  // namespace curiodb::storage

