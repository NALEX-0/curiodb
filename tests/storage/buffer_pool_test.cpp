#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

#include "curiodb/storage/buffer_pool.hpp"
#include "curiodb/storage/disk_manager.hpp"

namespace curiodb::storage {
namespace {

class TemporaryBufferFile {
 public:
  TemporaryBufferFile() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("curiodb_buffer_pool_test_" + std::to_string(suffix) + ".db");
  }
  ~TemporaryBufferFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

std::unique_ptr<DiskManager> manager(DiskOpenResult result) {
  EXPECT_TRUE(std::holds_alternative<std::unique_ptr<DiskManager>>(result));
  return std::get<std::unique_ptr<DiskManager>>(std::move(result));
}

PageId allocate(DiskManager& disk) {
  auto result = disk.allocate_page();
  EXPECT_TRUE(std::holds_alternative<PageId>(result));
  return std::get<PageId>(result);
}

TEST(BufferPoolTest, CachesAndPersistsDirtyPage) {
  TemporaryBufferFile file;
  auto disk = manager(open_disk_manager(file.path()));
  const PageId id = allocate(*disk);
  BufferPool pool{*disk, 2};
  {
    auto fetched = pool.fetch_page(id);
    ASSERT_TRUE(std::holds_alternative<PageGuard>(fetched));
    auto guard = std::get<PageGuard>(std::move(fetched));
    guard.bytes()[0] = std::byte{42};
    guard.mark_dirty();
  }
  ASSERT_TRUE(std::holds_alternative<std::monostate>(pool.flush_all()));

  const auto read = disk->read_page(id);
  ASSERT_TRUE(std::holds_alternative<PageBytes>(read));
  EXPECT_EQ(std::get<PageBytes>(read)[0], std::byte{42});
}

TEST(BufferPoolTest, EvictsLeastRecentlyUsedUnpinnedFrame) {
  TemporaryBufferFile file;
  auto disk = manager(open_disk_manager(file.path()));
  const PageId first = allocate(*disk);
  const PageId second = allocate(*disk);
  BufferPool pool{*disk, 1};
  {
    auto fetched = pool.fetch_page(first);
    ASSERT_TRUE(std::holds_alternative<PageGuard>(fetched));
  }

  EXPECT_TRUE(std::holds_alternative<PageGuard>(pool.fetch_page(second)));
  EXPECT_EQ(pool.size(), 1U);
}

TEST(BufferPoolTest, RefusesEvictionWhileEveryFrameIsPinned) {
  TemporaryBufferFile file;
  auto disk = manager(open_disk_manager(file.path()));
  const PageId first = allocate(*disk);
  const PageId second = allocate(*disk);
  BufferPool pool{*disk, 1};
  auto pinned = pool.fetch_page(first);
  ASSERT_TRUE(std::holds_alternative<PageGuard>(pinned));

  const auto result = pool.fetch_page(second);

  ASSERT_TRUE(std::holds_alternative<BufferPoolError>(result));
  EXPECT_EQ(std::get<BufferPoolError>(result).code,
            BufferPoolErrorCode::NoEvictableFrame);
}

TEST(BufferPoolTest, PageGuardAutomaticallyUnpinsOnDestruction) {
  TemporaryBufferFile file;
  auto disk = manager(open_disk_manager(file.path()));
  const PageId first = allocate(*disk);
  const PageId second = allocate(*disk);
  BufferPool pool{*disk, 1};
  {
    auto fetched = pool.fetch_page(first);
    ASSERT_TRUE(std::holds_alternative<PageGuard>(fetched));
    auto guard = std::get<PageGuard>(std::move(fetched));
  }

  EXPECT_TRUE(std::holds_alternative<PageGuard>(pool.fetch_page(second)));
}

}  // namespace
}  // namespace curiodb::storage
