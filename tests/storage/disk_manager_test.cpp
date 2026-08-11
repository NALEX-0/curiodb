#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <variant>

#include <gtest/gtest.h>

#include "curiodb/storage/disk_manager.hpp"
#include "curiodb/storage/slotted_page.hpp"

namespace curiodb::storage {
namespace {

class TemporaryDatabaseFile {
 public:
  TemporaryDatabaseFile() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("curiodb_disk_manager_test_" + std::to_string(suffix) + ".db");
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

std::unique_ptr<DiskManager> expect_manager(DiskOpenResult result) {
  EXPECT_TRUE(std::holds_alternative<std::unique_ptr<DiskManager>>(result));
  return std::get<std::unique_ptr<DiskManager>>(std::move(result));
}

PageId expect_page(PageAllocationResult result) {
  EXPECT_TRUE(std::holds_alternative<PageId>(result));
  return std::get<PageId>(result);
}

TEST(DiskManagerTest, CreatesNewEmptyDatabaseFile) {
  TemporaryDatabaseFile file;

  auto manager = expect_manager(open_disk_manager(file.path()));

  EXPECT_EQ(manager->path(), file.path());
  EXPECT_EQ(manager->page_count(), 0U);
  EXPECT_TRUE(std::filesystem::exists(file.path()));
  EXPECT_EQ(std::filesystem::file_size(file.path()), 0U);
}

TEST(DiskManagerTest, AllocatesSequentialZeroInitializedPages) {
  TemporaryDatabaseFile file;
  auto manager = expect_manager(open_disk_manager(file.path()));

  const PageId first = expect_page(manager->allocate_page());
  const PageId second = expect_page(manager->allocate_page());

  EXPECT_EQ(first, (PageId{0}));
  EXPECT_EQ(second, (PageId{1}));
  EXPECT_EQ(manager->page_count(), 2U);
  const auto read = manager->read_page(first);
  ASSERT_TRUE(std::holds_alternative<PageBytes>(read));
  EXPECT_EQ(std::get<PageBytes>(read), PageBytes{});
}

TEST(DiskManagerTest, WritesReadsAndFlushesExactPageImage) {
  TemporaryDatabaseFile file;
  auto manager = expect_manager(open_disk_manager(file.path()));
  const PageId page_id = expect_page(manager->allocate_page());
  SlottedPage page;
  const std::array<std::byte, 3> record{std::byte{1}, std::byte{2},
                                        std::byte{3}};
  ASSERT_TRUE(std::holds_alternative<SlotId>(page.insert_record(record)));

  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      manager->write_page(page_id, page.bytes())));
  EXPECT_TRUE(
      std::holds_alternative<std::monostate>(manager->flush()));
  const auto read = manager->read_page(page_id);

  ASSERT_TRUE(std::holds_alternative<PageBytes>(read));
  EXPECT_TRUE(std::ranges::equal(std::get<PageBytes>(read), page.bytes()));
}

TEST(DiskManagerTest, PersistsPagesAcrossReopen) {
  TemporaryDatabaseFile file;
  PageBytes expected;
  expected.fill(std::byte{42});
  {
    auto manager = expect_manager(open_disk_manager(file.path()));
    const PageId page_id = expect_page(manager->allocate_page());
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        manager->write_page(page_id, expected)));
    ASSERT_TRUE(
        std::holds_alternative<std::monostate>(manager->flush()));
  }

  auto reopened = expect_manager(open_disk_manager(file.path()));

  EXPECT_EQ(reopened->page_count(), 1U);
  const auto read = reopened->read_page(PageId{0});
  ASSERT_TRUE(std::holds_alternative<PageBytes>(read));
  EXPECT_EQ(std::get<PageBytes>(read), expected);
}

TEST(DiskManagerTest, KeepsPagesIndependent) {
  TemporaryDatabaseFile file;
  auto manager = expect_manager(open_disk_manager(file.path()));
  const PageId first = expect_page(manager->allocate_page());
  const PageId second = expect_page(manager->allocate_page());
  PageBytes first_bytes;
  first_bytes.fill(std::byte{1});
  PageBytes second_bytes;
  second_bytes.fill(std::byte{2});
  ASSERT_TRUE(std::holds_alternative<std::monostate>(
      manager->write_page(first, first_bytes)));
  ASSERT_TRUE(std::holds_alternative<std::monostate>(
      manager->write_page(second, second_bytes)));

  EXPECT_EQ(std::get<PageBytes>(manager->read_page(first)), first_bytes);
  EXPECT_EQ(std::get<PageBytes>(manager->read_page(second)), second_bytes);
}

TEST(DiskManagerTest, RejectsPageIdsOutsideAllocatedRange) {
  TemporaryDatabaseFile file;
  auto manager = expect_manager(open_disk_manager(file.path()));
  const PageBytes empty_page{};

  const auto read = manager->read_page(PageId{0});
  const auto write = manager->write_page(PageId{0}, empty_page);

  ASSERT_TRUE(std::holds_alternative<DiskError>(read));
  EXPECT_EQ(std::get<DiskError>(read).code, DiskErrorCode::InvalidPageId);
  ASSERT_TRUE(std::holds_alternative<DiskError>(write));
  EXPECT_EQ(std::get<DiskError>(write).code, DiskErrorCode::InvalidPageId);
}

TEST(DiskManagerTest, RejectsMisalignedExistingFile) {
  TemporaryDatabaseFile file;
  {
    std::ofstream output{file.path(), std::ios::binary};
    output.put('x');
  }

  const auto result = open_disk_manager(file.path());

  ASSERT_TRUE(std::holds_alternative<DiskError>(result));
  EXPECT_EQ(std::get<DiskError>(result).code,
            DiskErrorCode::MisalignedFile);
}

}  // namespace
}  // namespace curiodb::storage
