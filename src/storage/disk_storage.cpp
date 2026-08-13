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
#include "curiodb/storage/b_plus_tree.hpp"

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
    auto buffer_pool = std::make_unique<BufferPool>(*disk, 16);
    databases_.emplace(
        key, DatabaseState{.disk = std::move(disk),
                           .buffer_pool = std::move(buffer_pool),
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
  auto buffer_pool = std::make_unique<BufferPool>(*disk, 16);
  databases_.emplace(
      key, DatabaseState{.disk = std::move(disk),
                         .buffer_pool = std::move(buffer_pool),
                         .catalog = std::move(stored)});
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
  catalog::StoredTable stored_table{.schema = schema, .page_ids = {}};
  for (const auto& column : schema.columns) {
    if ((column.primary_key || column.unique) &&
        column.type.kind == DataTypeKind::Integer) {
      stored_table.indexes.push_back(
          {.name = "__curiodb_" + schema.name + "_" + column.name + "_key",
           .column_name = column.name,
           .root_page_id = std::nullopt});
    }
  }
  state->catalog.tables.push_back(std::move(stored_table));
  const auto stored = catalog::store_catalog(*state->disk, state->catalog);
  if (std::holds_alternative<catalog::CatalogStorageError>(stored)) {
    state->catalog.tables.pop_back();
    return wrapped_error(std::get<catalog::CatalogStorageError>(stored));
  }
  rebuild_catalog_view();
  return std::monostate{};
}

DiskStorageResult DiskStorage::create_index(
    std::string_view database, std::string name, std::string_view table,
    std::string_view column) {
  DatabaseState* const state = find_database(database);
  if (state == nullptr) {
    return error("database storage is unavailable");
  }
  for (const auto& candidate_table : state->catalog.tables) {
    for (const auto& candidate : candidate_table.indexes) {
      if (normalize(candidate.name) == normalize(name)) {
        return error("index '" + name + "' already exists");
      }
    }
  }
  catalog::StoredTable* const stored_table = find_table(*state, table);
  if (stored_table == nullptr) {
    return error("table storage is unavailable");
  }
  const std::string column_key = normalize(column);
  const auto found_column = std::find_if(
      stored_table->schema.columns.begin(), stored_table->schema.columns.end(),
      [&column_key](const catalog::ColumnSchema& candidate) {
        return normalize(candidate.name) == column_key;
      });
  if (found_column == stored_table->schema.columns.end()) {
    return error("column '" + std::string{column} + "' does not exist");
  }
  if (found_column->type.kind != DataTypeKind::Integer) {
    return error("B+ tree indexes currently support INT columns only");
  }
  const std::size_t column_index = static_cast<std::size_t>(std::distance(
      stored_table->schema.columns.begin(), found_column));
  TableHeap heap{*state->buffer_pool, *state->disk, stored_table->page_ids};
  auto scanned = heap.scan();
  if (const auto* scan_error = std::get_if<TableHeapError>(&scanned)) {
    return wrapped_error(*scan_error);
  }
  BPlusTree tree{*state->buffer_pool};
  for (const auto& heap_row : std::get<std::vector<HeapRow>>(scanned)) {
    const auto inserted =
        tree.insert(heap_row.row[column_index].as_integer(), heap_row.id);
    if (const auto* tree_error = std::get_if<BPlusTreeError>(&inserted)) {
      return error(tree_error->message);
    }
  }
  stored_table->indexes.push_back(
      {.name = std::move(name),
       .column_name = found_column->name,
       .root_page_id = tree.root_page_id()});
  const auto metadata = catalog::store_catalog(*state->disk, state->catalog);
  if (const auto* metadata_error =
          std::get_if<catalog::CatalogStorageError>(&metadata)) {
    stored_table->indexes.pop_back();
    return wrapped_error(*metadata_error);
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
  TableHeap heap{*state->buffer_pool, *state->disk, stored_table->page_ids};
  auto existing_rows = heap.scan();
  if (const auto* scan_error = std::get_if<TableHeapError>(&existing_rows)) {
    return wrapped_error(*scan_error);
  }
  for (std::size_t column_index = 0;
       column_index < stored_table->schema.columns.size(); ++column_index) {
    const auto& column = stored_table->schema.columns[column_index];
    if (!(column.primary_key || column.unique) ||
        column.type.kind == DataTypeKind::Integer) {
      continue;
    }
    for (const auto& heap_row : std::get<std::vector<HeapRow>>(existing_rows)) {
      if (heap_row.row[column_index] == row[column_index]) {
        return error("duplicate value for " +
                     std::string{column.primary_key ? "PRIMARY KEY '" : "UNIQUE column '"} +
                     column.name + "'");
      }
    }
  }
  for (const auto& index : stored_table->indexes) {
    const auto column = std::find_if(
        stored_table->schema.columns.begin(), stored_table->schema.columns.end(),
        [&index](const catalog::ColumnSchema& candidate) {
          return normalize(candidate.name) == normalize(index.column_name);
        });
    const std::size_t column_index = static_cast<std::size_t>(std::distance(
        stored_table->schema.columns.begin(), column));
    BPlusTree tree{*state->buffer_pool, index.root_page_id};
    const auto existing = tree.find(row[column_index].as_integer());
    if (const auto* tree_error = std::get_if<BPlusTreeError>(&existing)) {
      return error(tree_error->message);
    }
    if (std::get<std::optional<RowId>>(existing).has_value()) {
      return error("duplicate value for " +
                   std::string{column->primary_key ? "PRIMARY KEY '"
                                                   : column->unique
                                                         ? "UNIQUE column '"
                                                         : "unique index '"} +
                   (column->primary_key || column->unique ? column->name
                                                          : index.name) +
                   "'");
    }
  }
  const auto inserted = heap.insert(row);
  if (std::holds_alternative<TableHeapError>(inserted)) {
    return wrapped_error(std::get<TableHeapError>(inserted));
  }
  const RowId inserted_id = std::get<RowId>(inserted);
  for (auto& index : stored_table->indexes) {
    const auto column = std::find_if(
        stored_table->schema.columns.begin(), stored_table->schema.columns.end(),
        [&index](const catalog::ColumnSchema& candidate) {
          return normalize(candidate.name) == normalize(index.column_name);
        });
    const std::size_t column_index = static_cast<std::size_t>(std::distance(
        stored_table->schema.columns.begin(), column));
    BPlusTree tree{*state->buffer_pool, index.root_page_id};
    const auto indexed = tree.insert(row[column_index].as_integer(), inserted_id);
    if (const auto* tree_error = std::get_if<BPlusTreeError>(&indexed)) {
      return error(tree_error->message);
    }
    index.root_page_id = tree.root_page_id();
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
  TableHeap heap{*state->buffer_pool, *state->disk, stored_table->page_ids};
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

DiskRowsResult DiskStorage::index_scan(std::string_view database,
                                       std::string_view table,
                                       std::string_view column,
                                       std::int64_t key) {
  DatabaseState* const state = find_database(database);
  if (state == nullptr) {
    return error("database storage is unavailable");
  }
  catalog::StoredTable* const stored_table = find_table(*state, table);
  if (stored_table == nullptr) {
    return error("table storage is unavailable");
  }
  const auto index = std::find_if(
      stored_table->indexes.begin(), stored_table->indexes.end(),
      [&column](const catalog::StoredIndex& candidate) {
        return normalize(candidate.column_name) == normalize(column);
      });
  if (index == stored_table->indexes.end()) {
    return error("index storage is unavailable");
  }
  BPlusTree tree{*state->buffer_pool, index->root_page_id};
  const auto found = tree.find(key);
  if (const auto* tree_error = std::get_if<BPlusTreeError>(&found)) {
    return error(tree_error->message);
  }
  const auto& row_id = std::get<std::optional<RowId>>(found);
  if (!row_id.has_value()) {
    return std::vector<Row>{};
  }
  TableHeap heap{*state->buffer_pool, *state->disk, stored_table->page_ids};
  auto fetched = heap.fetch(*row_id);
  if (const auto* heap_error = std::get_if<TableHeapError>(&fetched)) {
    return wrapped_error(*heap_error);
  }
  return std::vector<Row>{std::get<Row>(std::move(fetched))};
}

std::vector<catalog::StoredIndex> DiskStorage::indexes(
    std::string_view database, std::string_view table) {
  DatabaseState* const state = find_database(database);
  if (state == nullptr) {
    return {};
  }
  catalog::StoredTable* const stored_table = find_table(*state, table);
  return stored_table == nullptr ? std::vector<catalog::StoredIndex>{}
                                 : stored_table->indexes;
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
  TableHeap heap{*state->buffer_pool, *state->disk, stored_table->page_ids};
  auto scanned = heap.scan();
  if (const auto* scan_error = std::get_if<TableHeapError>(&scanned)) {
    return wrapped_error(*scan_error);
  }
  std::size_t count = 0;
  for (const auto& heap_row : std::get<std::vector<HeapRow>>(scanned)) {
    if (!predicate(heap_row.row)) {
      continue;
    }
    for (const auto& index : stored_table->indexes) {
      const auto column = std::find_if(
          stored_table->schema.columns.begin(),
          stored_table->schema.columns.end(),
          [&index](const catalog::ColumnSchema& candidate) {
            return normalize(candidate.name) == normalize(index.column_name);
          });
      const std::size_t column_index = static_cast<std::size_t>(std::distance(
          stored_table->schema.columns.begin(), column));
      BPlusTree tree{*state->buffer_pool, index.root_page_id};
      const auto erased = tree.erase(heap_row.row[column_index].as_integer());
      if (const auto* tree_error = std::get_if<BPlusTreeError>(&erased)) {
        return error(tree_error->message);
      }
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
  TableHeap heap{*state->buffer_pool, *state->disk, stored_table->page_ids};
  auto scanned = heap.scan();
  if (const auto* scan_error = std::get_if<TableHeapError>(&scanned)) {
    return wrapped_error(*scan_error);
  }
  const auto& changed_column = stored_table->schema.columns[column_index];
  if (changed_column.primary_key || changed_column.unique) {
    std::size_t matching_count = 0;
    const HeapRow* matching_row = nullptr;
    for (const auto& candidate : std::get<std::vector<HeapRow>>(scanned)) {
      if (predicate(candidate.row)) {
        ++matching_count;
        matching_row = &candidate;
      }
    }
    const bool conflicts_with_other_row = std::any_of(
        std::get<std::vector<HeapRow>>(scanned).begin(),
        std::get<std::vector<HeapRow>>(scanned).end(),
        [&](const HeapRow& candidate) {
          return (matching_row == nullptr || candidate.id != matching_row->id) &&
                 candidate.row[column_index] == value;
        });
    if (matching_count > 1 || conflicts_with_other_row) {
      return error("duplicate value for " +
                   std::string{changed_column.primary_key
                                   ? "PRIMARY KEY '"
                                   : "UNIQUE column '"} +
                   changed_column.name + "'");
    }
  }
  std::size_t count = 0;
  for (auto& heap_row : std::get<std::vector<HeapRow>>(scanned)) {
    if (!predicate(heap_row.row)) {
      continue;
    }
    const Row old_row = heap_row.row;
    heap_row.row.set(column_index, value);
    for (const auto& index : stored_table->indexes) {
      const auto index_column = std::find_if(
          stored_table->schema.columns.begin(),
          stored_table->schema.columns.end(),
          [&index](const catalog::ColumnSchema& candidate) {
            return normalize(candidate.name) == normalize(index.column_name);
          });
      const std::size_t index_column_number =
          static_cast<std::size_t>(std::distance(
              stored_table->schema.columns.begin(), index_column));
      const std::int64_t old_key =
          old_row[index_column_number].as_integer();
      const std::int64_t new_key =
          heap_row.row[index_column_number].as_integer();
      if (old_key != new_key) {
        BPlusTree tree{*state->buffer_pool, index.root_page_id};
        const auto existing = tree.find(new_key);
        if (const auto* tree_error = std::get_if<BPlusTreeError>(&existing)) {
          return error(tree_error->message);
        }
        if (std::get<std::optional<RowId>>(existing).has_value()) {
          return error("duplicate value for " +
                       std::string{index_column->primary_key
                                       ? "PRIMARY KEY '"
                                       : index_column->unique
                                             ? "UNIQUE column '"
                                             : "unique index '"} +
                       (index_column->primary_key || index_column->unique
                            ? index_column->name
                            : index.name) +
                       "'");
        }
      }
    }
    const auto updated = heap.update(heap_row.id, heap_row.row);
    if (const auto* update_error = std::get_if<TableHeapError>(&updated)) {
      return wrapped_error(*update_error);
    }
    const RowId new_row_id = std::get<RowId>(updated);
    for (auto& index : stored_table->indexes) {
      const auto index_column = std::find_if(
          stored_table->schema.columns.begin(),
          stored_table->schema.columns.end(),
          [&index](const catalog::ColumnSchema& candidate) {
            return normalize(candidate.name) == normalize(index.column_name);
          });
      const std::size_t index_column_number =
          static_cast<std::size_t>(std::distance(
              stored_table->schema.columns.begin(), index_column));
      BPlusTree tree{*state->buffer_pool, index.root_page_id};
      auto changed = tree.erase(old_row[index_column_number].as_integer());
      if (const auto* tree_error = std::get_if<BPlusTreeError>(&changed)) {
        return error(tree_error->message);
      }
      changed = tree.insert(heap_row.row[index_column_number].as_integer(),
                            new_row_id);
      if (const auto* tree_error = std::get_if<BPlusTreeError>(&changed)) {
        return error(tree_error->message);
      }
      index.root_page_id = tree.root_page_id();
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
