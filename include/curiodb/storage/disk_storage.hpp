#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "curiodb/catalog/catalog_storage.hpp"
#include "curiodb/storage/disk_manager.hpp"
#include "curiodb/storage/buffer_pool.hpp"
#include "curiodb/storage/row.hpp"

namespace curiodb::storage {

struct DiskStorageError {
  std::string message;

  [[nodiscard]] friend bool operator==(
      const DiskStorageError&, const DiskStorageError&) = default;
};

using DiskStorageResult = std::variant<std::monostate, DiskStorageError>;
using DiskRowsResult = std::variant<std::vector<Row>, DiskStorageError>;
using DiskDeleteResult = std::variant<std::size_t, DiskStorageError>;
using DiskUpdateResult = std::variant<std::size_t, DiskStorageError>;

class DiskStorage {
 public:
  explicit DiskStorage(std::filesystem::path directory);

  [[nodiscard]] DiskStorageResult open();
  [[nodiscard]] const std::vector<catalog::StoredCatalog>& catalogs() const;
  [[nodiscard]] DiskStorageResult create_database(std::string name);
  [[nodiscard]] DiskStorageResult create_table(
      std::string_view database, const catalog::TableSchema& schema);
  [[nodiscard]] DiskStorageResult create_index(
      std::string_view database, std::string name, std::string_view table,
      std::string_view column);
  [[nodiscard]] DiskStorageResult insert(std::string_view database,
                                         std::string_view table, Row row);
  [[nodiscard]] DiskRowsResult scan(std::string_view database,
                                    std::string_view table);
  [[nodiscard]] DiskRowsResult index_scan(std::string_view database,
                                          std::string_view table,
                                          std::string_view column,
                                          std::int64_t key);
  [[nodiscard]] std::vector<catalog::StoredIndex> indexes(
      std::string_view database, std::string_view table);
  [[nodiscard]] DiskDeleteResult delete_where(
      std::string_view database, std::string_view table,
      const std::function<bool(const Row&)>& predicate);
  [[nodiscard]] DiskUpdateResult update_where(
      std::string_view database, std::string_view table,
      const std::function<bool(const Row&)>& predicate,
      std::size_t column_index, const Value& value);

 private:
  struct DatabaseState {
    std::unique_ptr<DiskManager> disk;
    std::unique_ptr<BufferPool> buffer_pool;
    catalog::StoredCatalog catalog;
  };

  void rebuild_catalog_view();
  [[nodiscard]] DatabaseState* find_database(std::string_view database);
  [[nodiscard]] catalog::StoredTable* find_table(DatabaseState& database,
                                                  std::string_view table);

  std::filesystem::path directory_;
  std::unordered_map<std::string, DatabaseState> databases_;
  std::vector<catalog::StoredCatalog> catalog_view_;
};

}  // namespace curiodb::storage
