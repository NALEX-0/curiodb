#pragma once

#include <string>
#include <variant>
#include <vector>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/storage/disk_manager.hpp"

namespace curiodb::catalog {

struct StoredTable {
  TableSchema schema;
  std::vector<storage::PageId> page_ids;

  [[nodiscard]] friend bool operator==(
      const StoredTable&, const StoredTable&) = default;
};

struct StoredCatalog {
  std::string database_name;
  std::vector<StoredTable> tables;

  [[nodiscard]] friend bool operator==(
      const StoredCatalog&, const StoredCatalog&) = default;
};

enum class CatalogStorageErrorCode {
  DiskError,
  DatabaseNotEmpty,
  MetadataTooLarge,
  InvalidMagic,
  UnsupportedVersion,
  CorruptMetadata,
};

struct CatalogStorageError {
  CatalogStorageErrorCode code;
  std::string message;

  [[nodiscard]] friend bool operator==(
      const CatalogStorageError&, const CatalogStorageError&) = default;
};

using CatalogStorageResult =
    std::variant<std::monostate, CatalogStorageError>;
using CatalogLoadResult =
    std::variant<StoredCatalog, CatalogStorageError>;

inline constexpr storage::PageId kCatalogPageId{0};

[[nodiscard]] CatalogStorageResult initialize_catalog_storage(
    storage::DiskManager& disk_manager, const StoredCatalog& catalog);
[[nodiscard]] CatalogStorageResult store_catalog(
    storage::DiskManager& disk_manager, const StoredCatalog& catalog);
[[nodiscard]] CatalogLoadResult load_catalog(
    storage::DiskManager& disk_manager);

}  // namespace curiodb::catalog
