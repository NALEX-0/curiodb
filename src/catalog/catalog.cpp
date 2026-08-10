#include "curiodb/catalog/catalog.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace curiodb::catalog {
namespace {

std::string normalize_identifier(std::string_view identifier) {
  std::string normalized;
  normalized.reserve(identifier.size());
  for (const char character : identifier) {
    normalized.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(character))));
  }
  return normalized;
}

CatalogError error(CatalogErrorCode code, std::string message) {
  return CatalogError{code, std::move(message)};
}

}  // namespace

CatalogResult Catalog::create_database(std::string name) {
  if (name.empty()) {
    return error(CatalogErrorCode::InvalidName,
                 "database name cannot be empty");
  }

  const std::string key = normalize_identifier(name);
  if (databases_.contains(key)) {
    return error(CatalogErrorCode::DatabaseAlreadyExists,
                 "database '" + name + "' already exists");
  }
  databases_.emplace(key, Database{.name = std::move(name), .tables = {}});
  return std::nullopt;
}

CatalogResult Catalog::use_database(std::string_view name) {
  const std::string key = normalize_identifier(name);
  if (!databases_.contains(key)) {
    return error(CatalogErrorCode::DatabaseNotFound,
                 "database '" + std::string{name} + "' does not exist");
  }
  active_database_key_ = key;
  return std::nullopt;
}

CatalogResult Catalog::create_table(std::string name,
                                    std::vector<ColumnSchema> columns) {
  Database* const database = selected_database();
  if (database == nullptr) {
    return error(CatalogErrorCode::NoDatabaseSelected,
                 "no database selected; use USE <database> first");
  }
  if (name.empty()) {
    return error(CatalogErrorCode::InvalidName, "table name cannot be empty");
  }

  const std::string table_key = normalize_identifier(name);
  if (database->tables.contains(table_key)) {
    return error(CatalogErrorCode::TableAlreadyExists,
                 "table '" + name + "' already exists");
  }
  if (columns.empty()) {
    return error(CatalogErrorCode::EmptyTable,
                 "table must contain at least one column");
  }

  std::unordered_set<std::string> column_names;
  for (const auto& column : columns) {
    if (column.name.empty()) {
      return error(CatalogErrorCode::InvalidName,
                   "column name cannot be empty");
    }
    if (!column_names.insert(normalize_identifier(column.name)).second) {
      return error(CatalogErrorCode::DuplicateColumn,
                   "duplicate column '" + column.name + "'");
    }
  }

  database->tables.emplace(
      table_key,
      TableSchema{.name = std::move(name), .columns = std::move(columns)});
  return std::nullopt;
}

std::optional<std::string> Catalog::active_database() const {
  const Database* const database = selected_database();
  if (database == nullptr) {
    return std::nullopt;
  }
  return database->name;
}

std::vector<std::string> Catalog::database_names() const {
  std::vector<std::string> names;
  names.reserve(databases_.size());
  for (const auto& [key, database] : databases_) {
    static_cast<void>(key);
    names.push_back(database.name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::vector<std::string> Catalog::table_names() const {
  const Database* const database = selected_database();
  if (database == nullptr) {
    return {};
  }

  std::vector<std::string> names;
  names.reserve(database->tables.size());
  for (const auto& [key, table] : database->tables) {
    static_cast<void>(key);
    names.push_back(table.name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

const TableSchema* Catalog::find_table(std::string_view name) const {
  const Database* const database = selected_database();
  if (database == nullptr) {
    return nullptr;
  }
  const auto table = database->tables.find(normalize_identifier(name));
  return table == database->tables.end() ? nullptr : &table->second;
}

const Catalog::Database* Catalog::selected_database() const {
  if (!active_database_key_.has_value()) {
    return nullptr;
  }
  const auto database = databases_.find(*active_database_key_);
  return database == databases_.end() ? nullptr : &database->second;
}

Catalog::Database* Catalog::selected_database() {
  if (!active_database_key_.has_value()) {
    return nullptr;
  }
  const auto database = databases_.find(*active_database_key_);
  return database == databases_.end() ? nullptr : &database->second;
}

}  // namespace curiodb::catalog

