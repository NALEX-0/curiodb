#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

#include "curiodb/catalog/catalog_storage.hpp"
#include "curiodb/storage/disk_manager.hpp"
#include "curiodb/storage/table_heap.hpp"
#include "curiodb/types/data_type.hpp"

namespace curiodb::catalog {
namespace {

class TemporaryDatabaseFile {
 public:
  TemporaryDatabaseFile() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("curiodb_catalog_storage_test_" + std::to_string(suffix) +
             ".db");
  }

  ~TemporaryDatabaseFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

std::unique_ptr<storage::DiskManager> expect_manager(
    storage::DiskOpenResult result) {
  EXPECT_TRUE(std::holds_alternative<std::unique_ptr<storage::DiskManager>>(
      result));
  return std::get<std::unique_ptr<storage::DiskManager>>(std::move(result));
}

StoredCatalog sample_catalog() {
  return {
      .database_name = "company",
      .tables = {{
          .schema =
              {.name = "employees",
               .columns =
                   {{.name = "id",
                     .type = {.kind = DataTypeKind::Integer},
                     .primary_key = true,
                     .unique = true,
                     .not_null = true},
                    {.name = "name",
                     .type = {.kind = DataTypeKind::Varchar, .length = 100}},
                    {.name = "salary",
                     .type = {.kind = DataTypeKind::Double}}}},
          .page_ids = {storage::PageId{1}, storage::PageId{4}},
      }},
  };
}

TEST(CatalogStorageTest, InitializesCatalogAtPageZero) {
  TemporaryDatabaseFile file;
  auto disk = expect_manager(storage::open_disk_manager(file.path()));

  const auto result = initialize_catalog_storage(*disk, sample_catalog());

  EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
  EXPECT_EQ(disk->page_count(), 1U);
}

TEST(CatalogStorageTest, PersistsSchemaAndTablePageListsAcrossReopen) {
  TemporaryDatabaseFile file;
  const StoredCatalog expected = sample_catalog();
  {
    auto disk = expect_manager(storage::open_disk_manager(file.path()));
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        initialize_catalog_storage(*disk, expected)));
  }

  auto disk = expect_manager(storage::open_disk_manager(file.path()));
  const auto loaded = load_catalog(*disk);

  ASSERT_TRUE(std::holds_alternative<StoredCatalog>(loaded));
  EXPECT_EQ(std::get<StoredCatalog>(loaded), expected);
}

TEST(CatalogStorageTest, UpdatesCatalogAfterTableHeapAllocatesPages) {
  TemporaryDatabaseFile file;
  auto disk = expect_manager(storage::open_disk_manager(file.path()));
  StoredCatalog catalog{.database_name = "company", .tables = {}};
  ASSERT_TRUE(std::holds_alternative<std::monostate>(
      initialize_catalog_storage(*disk, catalog)));
  storage::TableHeap heap{*disk};
  ASSERT_TRUE(std::holds_alternative<storage::RowId>(
      heap.insert(storage::Row{Value{std::int64_t{1}}})));
  catalog.tables.push_back({
      .schema = {.name = "items",
                 .columns = {{.name = "id",
                              .type = {.kind = DataTypeKind::Integer}}}},
      .page_ids = {heap.page_ids().begin(), heap.page_ids().end()},
  });

  ASSERT_TRUE(std::holds_alternative<std::monostate>(
      store_catalog(*disk, catalog)));
  const auto loaded = load_catalog(*disk);

  ASSERT_TRUE(std::holds_alternative<StoredCatalog>(loaded));
  EXPECT_EQ(std::get<StoredCatalog>(loaded), catalog);
  EXPECT_EQ(catalog.tables[0].page_ids,
            (std::vector<storage::PageId>{storage::PageId{1}}));
}

TEST(CatalogStorageTest, RejectsInitializationOfNonEmptyDatabase) {
  TemporaryDatabaseFile file;
  auto disk = expect_manager(storage::open_disk_manager(file.path()));
  ASSERT_TRUE(std::holds_alternative<storage::PageId>(
      disk->allocate_page()));

  const auto result = initialize_catalog_storage(*disk, sample_catalog());

  ASSERT_TRUE(std::holds_alternative<CatalogStorageError>(result));
  EXPECT_EQ(std::get<CatalogStorageError>(result).code,
            CatalogStorageErrorCode::DatabaseNotEmpty);
}

TEST(CatalogStorageTest, RejectsPageWithoutCatalogSignature) {
  TemporaryDatabaseFile file;
  auto disk = expect_manager(storage::open_disk_manager(file.path()));
  ASSERT_TRUE(std::holds_alternative<storage::PageId>(
      disk->allocate_page()));

  const auto result = load_catalog(*disk);

  ASSERT_TRUE(std::holds_alternative<CatalogStorageError>(result));
  EXPECT_EQ(std::get<CatalogStorageError>(result).code,
            CatalogStorageErrorCode::InvalidMagic);
}

TEST(CatalogStorageTest, RejectsMetadataLargerThanCatalogPage) {
  TemporaryDatabaseFile file;
  auto disk = expect_manager(storage::open_disk_manager(file.path()));
  StoredCatalog catalog{.database_name = std::string(5000, 'x'),
                        .tables = {}};

  const auto result = initialize_catalog_storage(*disk, catalog);

  ASSERT_TRUE(std::holds_alternative<CatalogStorageError>(result));
  EXPECT_EQ(std::get<CatalogStorageError>(result).code,
            CatalogStorageErrorCode::MetadataTooLarge);
  EXPECT_EQ(disk->page_count(), 0U);
}

}  // namespace
}  // namespace curiodb::catalog
