#include "curiodb/storage/in_memory_storage.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace curiodb::storage {
namespace {

std::string normalize(std::string_view name) {
  std::string result;
  result.reserve(name.size());
  for (const char character : name) {
    result.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

}  // namespace

void InMemoryStorage::create_table(std::string_view database,
                                   const catalog::TableSchema& schema) {
  databases_[normalize(database)].try_emplace(normalize(schema.name), schema);
}

InMemoryTable* InMemoryStorage::find_table(std::string_view database,
                                           std::string_view table) {
  const auto database_entry = databases_.find(normalize(database));
  if (database_entry == databases_.end()) {
    return nullptr;
  }
  const auto table_entry = database_entry->second.find(normalize(table));
  return table_entry == database_entry->second.end() ? nullptr
                                                      : &table_entry->second;
}

const InMemoryTable* InMemoryStorage::find_table(
    std::string_view database, std::string_view table) const {
  const auto database_entry = databases_.find(normalize(database));
  if (database_entry == databases_.end()) {
    return nullptr;
  }
  const auto table_entry = database_entry->second.find(normalize(table));
  return table_entry == database_entry->second.end() ? nullptr
                                                      : &table_entry->second;
}

}  // namespace curiodb::storage

