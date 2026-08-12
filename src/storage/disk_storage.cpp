#include "curiodb/storage/disk_storage.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "curiodb/catalog/catalog_storage.hpp"
#include "curiodb/storage/row_validation.hpp"
#include "curiodb/storage/table_heap.hpp"

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

DiskStorageError error(std::string message) { return {std::move(message)}; }

template <typename Error>
DiskStorageError wrapped_error(const Error& source) {
  return error(source.message);
}

}  // namespace

DiskStorage::DiskStorage(std::filesystem::path directory)
    : directory_(std::move(directory)) {}

DiskStorageResult DiskStorage::open() {
  std::error_code filesystem_error;
  std::filesystem::create_directories(directory_, filesystem_error);
  if (filesystem_error) {
    return error("failed to create database directory");
  }

  databases_.clear();
  for (const auto& entry :
       std::filesystem::directory_iterator(directory_, filesystem_error)) {
    if (filesystem_error) {
      return error("failed to read database directory");
    }
    if (!entry.is_regular_file() || entry.path().extension() != ".cdb") {
      continue;
    }
    auto opened = open_disk_manager(entry.path());
    if (std::holds_alternative<DiskError>(opened)) {
      return wrapped_error(std::get<DiskError>(opened));
    }
    auto disk = std::get<std::unique_ptr<DiskManager>>(std::move(opened));
    auto loaded = catalog::load_catalog(*disk);
    if (std::holds_alternative<catalog::CatalogStorageError>(loaded)) {
      return wrapped_error(
          std::get<catalog::CatalogStorageError>(loaded));
    }
    auto stored = std::get<catalog::StoredCatalog>(std::move(loaded));
    const std::string key = normalize(stored.database_name);
    if (databases_.contains(key)) {
      return error("database directory contains duplicate database names");
    }
    databases_.emplace(
        key, DatabaseState{.disk = std::move(disk),
                           .catalog = std::move(stored)});
  }
  if (filesystem_error) {
    return error("failed to read database directory");
  }
  rebuild_catalog_view();
  return std::monostate{};
}

const std::vector<catalog::StoredCatalog>& DiskStorage::catalogs() const {
  return catalog_view_;
}

DiskStorageResult DiskStorage::create_database(std::string name) {
  const std::string key = normalize(name);
  if (databases_.contains(key)) {
    return error("database '" + name + "' already exists on disk");
  }
  const auto path = directory_ / (key + ".cdb");
  if (std::filesystem::exists(path)) {
    return error("database file already exists");
  }
  auto opened = open_disk_manager(path);
  if (std::holds_alternative<DiskError>(opened)) {
    return wrapped_error(std::get<DiskError>(opened));
  }
  auto disk = std::get<std::unique_ptr<DiskManager>>(std::move(opened));
  catalog::StoredCatalog stored{.database_name = std::move(name), .tables = {}};
  const auto initialized = catalog::initialize_catalog_storage(*disk, stored);
  if (std::holds_alternative<catalog::CatalogStorageError>(initialized)) {
    std::error_code ignored;
    disk.reset();
    std::filesystem::remove(path, ignored);
    return wrapped_error(
        std::get<catalog::CatalogStorageError>(initialized));
  }
  databases_.emplace(
      key, DatabaseState{.disk = std::move(disk), .catalog = std::move(stored)});
  rebuild_catalog_view();
  return std::monostate{};
}

DiskStorageResult DiskStorage::create_table(
    std::string_view database, const catalog::TableSchema& schema) {
  DatabaseState* const state = find_database(database);
  if (state == nullptr) {
    return error("database storage is unavailable");
  }
  if (find_table(*state, schema.name) != nullptr) {
    return error("table storage already exists");
  }
  state->catalog.tables.push_back(
      catalog::StoredTable{.schema = schema, .page_ids = {}});
  const auto stored = catalog::store_catalog(*state->disk, state->catalog);
  if (std::holds_alternative<catalog::CatalogStorageError>(stored)) {
    state->catalog.tables.pop_back();
    return wrapped_error(std::get<catalog::CatalogStorageError>(stored));
  }
  rebuild_catalog_view();
  return std::monostate{};
}

DiskStorageResult DiskStorage::insert(std::string_view database,
                                      std::string_view table, Row row) {
  DatabaseState* const state = find_database(database);
  if (state == nullptr) {
    return error("database storage is unavailable");
  }
  catalog::StoredTable* const stored_table = find_table(*state, table);
  if (stored_table == nullptr) {
    return error("table storage is unavailable");
  }
  if (const auto validation = validate_row(stored_table->schema, row);
      validation.has_value()) {
    return error(validation->message);
  }
  TableHeap heap{*state->disk, stored_table->page_ids};
  const auto inserted = heap.insert(row);
  if (std::holds_alternative<TableHeapError>(inserted)) {
    return wrapped_error(std::get<TableHeapError>(inserted));
  }
  stored_table->page_ids.assign(heap.page_ids().begin(), heap.page_ids().end());
  const auto metadata = catalog::store_catalog(*state->disk, state->catalog);
  if (std::holds_alternative<catalog::CatalogStorageError>(metadata)) {
    return wrapped_error(std::get<catalog::CatalogStorageError>(metadata));
  }
  rebuild_catalog_view();
  return std::monostate{};
}

DiskRowsResult DiskStorage::scan(std::string_view database,
                                 std::string_view table) {
  DatabaseState* const state = find_database(database);
  if (state == nullptr) {
    return error("database storage is unavailable");
  }
  catalog::StoredTable* const stored_table = find_table(*state, table);
  if (stored_table == nullptr) {
    return error("table storage is unavailable");
  }
  TableHeap heap{*state->disk, stored_table->page_ids};
  auto scanned = heap.scan();
  if (std::holds_alternative<TableHeapError>(scanned)) {
    return wrapped_error(std::get<TableHeapError>(scanned));
  }
  std::vector<Row> rows;
  auto heap_rows = std::get<std::vector<HeapRow>>(std::move(scanned));
  rows.reserve(heap_rows.size());
  for (auto& heap_row : heap_rows) {
    rows.push_back(std::move(heap_row.row));
  }
  return rows;
}

DiskDeleteResult DiskStorage::delete_where(
    std::string_view database, std::string_view table,
    const std::function<bool(const Row&)>& predicate) {
  DatabaseState* const state = find_database(database);
  if (state == nullptr) {
    return error("database storage is unavailable");
  }
  catalog::StoredTable* const stored_table = find_table(*state, table);
  if (stored_table == nullptr) {
    return error("table storage is unavailable");
  }
  TableHeap heap{*state->disk, stored_table->page_ids};
  auto scanned = heap.scan();
  if (const auto* scan_error = std::get_if<TableHeapError>(&scanned)) {
    return wrapped_error(*scan_error);
  }
  std::size_t count = 0;
  for (const auto& heap_row : std::get<std::vector<HeapRow>>(scanned)) {
    if (!predicate(heap_row.row)) {
      continue;
    }
    const auto deleted = heap.delete_row(heap_row.id);
    if (const auto* delete_error = std::get_if<TableHeapError>(&deleted)) {
      return wrapped_error(*delete_error);
    }
    ++count;
  }
  const auto flushed = heap.flush();
  if (const auto* flush_error = std::get_if<DiskError>(&flushed)) {
    return wrapped_error(*flush_error);
  }
  return count;
}

DiskUpdateResult DiskStorage::update_where(
    std::string_view database, std::string_view table,
    const std::function<bool(const Row&)>& predicate,
    std::size_t column_index, const Value& value) {
  DatabaseState* const state = find_database(database);
  if (state == nullptr) {
    return error("database storage is unavailable");
  }
  catalog::StoredTable* const stored_table = find_table(*state, table);
  if (stored_table == nullptr) {
    return error("table storage is unavailable");
  }
  TableHeap heap{*state->disk, stored_table->page_ids};
  auto scanned = heap.scan();
  if (const auto* scan_error = std::get_if<TableHeapError>(&scanned)) {
    return wrapped_error(*scan_error);
  }
  std::size_t count = 0;
  for (auto& heap_row : std::get<std::vector<HeapRow>>(scanned)) {
    if (!predicate(heap_row.row)) {
      continue;
    }
    heap_row.row.set(column_index, value);
    const auto updated = heap.update(heap_row.id, heap_row.row);
    if (const auto* update_error = std::get_if<TableHeapError>(&updated)) {
      return wrapped_error(*update_error);
    }
    ++count;
  }
  stored_table->page_ids.assign(heap.page_ids().begin(), heap.page_ids().end());
  const auto metadata = catalog::store_catalog(*state->disk, state->catalog);
  if (const auto* metadata_error =
          std::get_if<catalog::CatalogStorageError>(&metadata)) {
    return wrapped_error(*metadata_error);
  }
  rebuild_catalog_view();
  return count;
}

void DiskStorage::rebuild_catalog_view() {
  catalog_view_.clear();
  catalog_view_.reserve(databases_.size());
  for (const auto& [key, database] : databases_) {
    static_cast<void>(key);
    catalog_view_.push_back(database.catalog);
  }
  std::sort(catalog_view_.begin(), catalog_view_.end(),
            [](const auto& left, const auto& right) {
              return left.database_name < right.database_name;
            });
}

DiskStorage::DatabaseState* DiskStorage::find_database(
    std::string_view database) {
  const auto found = databases_.find(normalize(database));
  return found == databases_.end() ? nullptr : &found->second;
}

catalog::StoredTable* DiskStorage::find_table(DatabaseState& database,
                                               std::string_view table) {
  const std::string key = normalize(table);
  const auto found = std::find_if(
      database.catalog.tables.begin(), database.catalog.tables.end(),
      [&key](const catalog::StoredTable& candidate) {
        return normalize(candidate.schema.name) == key;
      });
  return found == database.catalog.tables.end() ? nullptr : &*found;
}

}  // namespace curiodb::storage
