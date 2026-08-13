#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "curiodb/storage/b_plus_tree.hpp"
#include "curiodb/storage/buffer_pool.hpp"
#include "curiodb/storage/disk_manager.hpp"
#include "curiodb/storage/table_heap.hpp"

namespace curiodb::storage {
namespace {

class TemporaryIndexFile {
 public:
  TemporaryIndexFile() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("curiodb_b_plus_tree_test_" + std::to_string(suffix) + ".db");
  }
  ~TemporaryIndexFile() {
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

RowId row_id(std::int64_t key) {
  const auto normalized = static_cast<std::uint32_t>(key + 1000);
  return {PageId{normalized},
          SlotId{static_cast<std::uint16_t>(normalized)}};
}

TEST(BPlusTreeTest, StartsEmptyAndFindsInsertedKey) {
  TemporaryIndexFile file;
  auto disk = manager(open_disk_manager(file.path()));
  BufferPool pool{*disk, 8};
  BPlusTree tree{pool, std::nullopt, 3};

  EXPECT_FALSE(tree.root_page_id().has_value());
  EXPECT_TRUE(std::holds_alternative<std::monostate>(tree.insert(7, row_id(7))));
  const auto found = tree.find(7);
  ASSERT_TRUE(std::holds_alternative<std::optional<RowId>>(found));
  EXPECT_EQ(std::get<std::optional<RowId>>(found), row_id(7));
  EXPECT_TRUE(tree.root_page_id().has_value());
}

TEST(BPlusTreeTest, SplitsLeavesAndInternalNodesForShuffledKeys) {
  TemporaryIndexFile file;
  auto disk = manager(open_disk_manager(file.path()));
  BufferPool pool{*disk, 4};
  BPlusTree tree{pool, std::nullopt, 3};
  std::vector<std::int64_t> keys;
  for (std::int64_t key = 0; key < 100; ++key) {
    keys.push_back(key);
  }
  std::mt19937 generator{42};
  std::shuffle(keys.begin(), keys.end(), generator);
  for (const std::int64_t key : keys) {
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        tree.insert(key, row_id(key))));
  }

  for (std::int64_t key = 0; key < 100; ++key) {
    const auto found = tree.find(key);
    ASSERT_TRUE(std::holds_alternative<std::optional<RowId>>(found));
    EXPECT_EQ(std::get<std::optional<RowId>>(found), row_id(key));
  }
  EXPECT_GT(disk->page_count(), 10U);
}

TEST(BPlusTreeTest, ReturnsInclusiveRangeInKeyOrder) {
  TemporaryIndexFile file;
  auto disk = manager(open_disk_manager(file.path()));
  BufferPool pool{*disk, 3};
  BPlusTree tree{pool, std::nullopt, 3};
  for (std::int64_t key = 20; key >= 0; --key) {
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        tree.insert(key, row_id(key))));
  }

  const auto result = tree.range(5, 12);

  ASSERT_TRUE(std::holds_alternative<std::vector<RowId>>(result));
  const auto& rows = std::get<std::vector<RowId>>(result);
  ASSERT_EQ(rows.size(), 8U);
  for (std::size_t index = 0; index < rows.size(); ++index) {
    EXPECT_EQ(rows[index], row_id(static_cast<std::int64_t>(index + 5)));
  }
}

TEST(BPlusTreeTest, RejectsDuplicateKeysWithoutChangingValue) {
  TemporaryIndexFile file;
  auto disk = manager(open_disk_manager(file.path()));
  BufferPool pool{*disk, 4};
  BPlusTree tree{pool, std::nullopt, 3};
  ASSERT_TRUE(std::holds_alternative<std::monostate>(
      tree.insert(4, row_id(4))));

  const auto duplicate = tree.insert(4, row_id(9));

  ASSERT_TRUE(std::holds_alternative<BPlusTreeError>(duplicate));
  EXPECT_EQ(std::get<BPlusTreeError>(duplicate).code,
            BPlusTreeErrorCode::DuplicateKey);
  EXPECT_EQ(std::get<std::optional<RowId>>(tree.find(4)), row_id(4));
}

TEST(BPlusTreeTest, ReopensFromPersistedRootPage) {
  TemporaryIndexFile file;
  PageId root;
  {
    auto disk = manager(open_disk_manager(file.path()));
    BufferPool pool{*disk, 4};
    BPlusTree tree{pool, std::nullopt, 3};
    for (std::int64_t key = 0; key < 40; ++key) {
      ASSERT_TRUE(std::holds_alternative<std::monostate>(
          tree.insert(key, row_id(key))));
    }
    root = *tree.root_page_id();
    ASSERT_TRUE(std::holds_alternative<std::monostate>(tree.flush()));
  }

  auto disk = manager(open_disk_manager(file.path()));
  BufferPool pool{*disk, 4};
  BPlusTree reopened{pool, root, 3};
  EXPECT_EQ(std::get<std::optional<RowId>>(reopened.find(0)), row_id(0));
  EXPECT_EQ(std::get<std::optional<RowId>>(reopened.find(39)), row_id(39));
  EXPECT_FALSE(std::get<std::optional<RowId>>(reopened.find(100)).has_value());
}

TEST(BPlusTreeTest, OrdersNegativeAndPositiveKeys) {
  TemporaryIndexFile file;
  auto disk = manager(open_disk_manager(file.path()));
  BufferPool pool{*disk, 4};
  BPlusTree tree{pool, std::nullopt, 3};
  for (const std::int64_t key : {5, -10, 0, -2, 12}) {
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        tree.insert(key, row_id(key))));
  }

  const auto result = tree.range(-2, 5);

  ASSERT_TRUE(std::holds_alternative<std::vector<RowId>>(result));
  EXPECT_EQ(std::get<std::vector<RowId>>(result),
            (std::vector<RowId>{row_id(-2), row_id(0), row_id(5)}));
}

TEST(BPlusTreeTest, RejectsPageWithoutIndexHeader) {
  TemporaryIndexFile file;
  auto disk = manager(open_disk_manager(file.path()));
  const auto allocated = disk->allocate_page();
  ASSERT_TRUE(std::holds_alternative<PageId>(allocated));
  BufferPool pool{*disk, 4};
  BPlusTree tree{pool, std::get<PageId>(allocated), 3};

  const auto result = tree.find(1);

  ASSERT_TRUE(std::holds_alternative<BPlusTreeError>(result));
  EXPECT_EQ(std::get<BPlusTreeError>(result).code,
            BPlusTreeErrorCode::CorruptNode);
}

TEST(BPlusTreeTest, ErasesKeyWithoutAffectingOtherLeaves) {
  TemporaryIndexFile file;
  auto disk = manager(open_disk_manager(file.path()));
  BufferPool pool{*disk, 4};
  BPlusTree tree{pool, std::nullopt, 3};
  for (std::int64_t key = 0; key < 20; ++key) {
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        tree.insert(key, row_id(key))));
  }

  EXPECT_TRUE(std::holds_alternative<std::monostate>(tree.erase(8)));
  EXPECT_FALSE(std::get<std::optional<RowId>>(tree.find(8)).has_value());
  EXPECT_EQ(std::get<std::optional<RowId>>(tree.find(7)), row_id(7));
  EXPECT_EQ(std::get<std::optional<RowId>>(tree.find(9)), row_id(9));
}

}  // namespace
}  // namespace curiodb::storage
