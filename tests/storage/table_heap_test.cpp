#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "curiodb/storage/disk_manager.hpp"
#include "curiodb/storage/row.hpp"
#include "curiodb/storage/table_heap.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::storage {
namespace {

class TemporaryHeapFile {
 public:
  TemporaryHeapFile() {
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("curiodb_table_heap_test_" + std::to_string(suffix) + ".db");
  }

  ~TemporaryHeapFile() {
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

RowId expect_row_id(HeapInsertResult result) {
  EXPECT_TRUE(std::holds_alternative<RowId>(result));
  return std::get<RowId>(result);
}

Row make_row(std::int64_t id, std::string name) {
  return Row{Value{id}, Value{std::move(name)}};
}

TEST(TableHeapTest, InsertsAndFetchesRowByStableId) {
  TemporaryHeapFile file;
  auto disk = expect_manager(open_disk_manager(file.path()));
  TableHeap heap{*disk};
  const Row original = make_row(1, "Alice");

  const RowId row_id = expect_row_id(heap.insert(original));
  const auto fetched = heap.fetch(row_id);

  EXPECT_EQ(row_id, (RowId{PageId{0}, SlotId{0}}));
  ASSERT_TRUE(std::holds_alternative<Row>(fetched));
  EXPECT_EQ(std::get<Row>(fetched), original);
}

TEST(TableHeapTest, ScansRowsInInsertionOrder) {
  TemporaryHeapFile file;
  auto disk = expect_manager(open_disk_manager(file.path()));
  TableHeap heap{*disk};
  const RowId first = expect_row_id(heap.insert(make_row(1, "Alice")));
  const RowId second = expect_row_id(heap.insert(make_row(2, "Bob")));

  const auto scan = heap.scan();

  ASSERT_TRUE(std::holds_alternative<std::vector<HeapRow>>(scan));
  const auto& rows = std::get<std::vector<HeapRow>>(scan);
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].id, first);
  EXPECT_EQ(rows[0].row, make_row(1, "Alice"));
  EXPECT_EQ(rows[1].id, second);
  EXPECT_EQ(rows[1].row, make_row(2, "Bob"));
}

TEST(TableHeapTest, GrowsAcrossMultiplePages) {
  TemporaryHeapFile file;
  auto disk = expect_manager(open_disk_manager(file.path()));
  TableHeap heap{*disk};
  const std::string payload(1000, 'x');
  std::vector<RowId> ids;
  for (std::int64_t id = 0; id < 12; ++id) {
    ids.push_back(expect_row_id(heap.insert(make_row(id, payload))));
  }

  EXPECT_GT(heap.page_ids().size(), 1U);
  EXPECT_NE(ids.front().page_id, ids.back().page_id);
  const auto scan = heap.scan();
  ASSERT_TRUE(std::holds_alternative<std::vector<HeapRow>>(scan));
  const auto& rows = std::get<std::vector<HeapRow>>(scan);
  ASSERT_EQ(rows.size(), 12U);
  for (std::size_t index = 0; index < rows.size(); ++index) {
    EXPECT_EQ(rows[index].row[0].as_integer(),
              static_cast<std::int64_t>(index));
  }
}

TEST(TableHeapTest, ReopensUsingPersistedPageList) {
  TemporaryHeapFile file;
  std::vector<PageId> page_ids;
  RowId row_id;
  {
    auto disk = expect_manager(open_disk_manager(file.path()));
    TableHeap heap{*disk};
    row_id = expect_row_id(heap.insert(make_row(7, "Persistent")));
    page_ids.assign(heap.page_ids().begin(), heap.page_ids().end());
    ASSERT_TRUE(std::holds_alternative<std::monostate>(heap.flush()));
  }

  auto disk = expect_manager(open_disk_manager(file.path()));
  TableHeap reopened{*disk, page_ids};
  const auto fetched = reopened.fetch(row_id);

  ASSERT_TRUE(std::holds_alternative<Row>(fetched));
  EXPECT_EQ(std::get<Row>(fetched), make_row(7, "Persistent"));
}

TEST(TableHeapTest, RejectsRowTooLargeForEmptyPage) {
  TemporaryHeapFile file;
  auto disk = expect_manager(open_disk_manager(file.path()));
  TableHeap heap{*disk};

  const auto result = heap.insert(make_row(1, std::string(5000, 'x')));

  ASSERT_TRUE(std::holds_alternative<TableHeapError>(result));
  EXPECT_EQ(std::get<TableHeapError>(result).code,
            TableHeapErrorCode::RowTooLarge);
  EXPECT_TRUE(heap.page_ids().empty());
  EXPECT_EQ(disk->page_count(), 0U);
}

TEST(TableHeapTest, RejectsRowIdsFromOutsideTable) {
  TemporaryHeapFile file;
  auto disk = expect_manager(open_disk_manager(file.path()));
  TableHeap heap{*disk};

  const auto result = heap.fetch(RowId{PageId{0}, SlotId{0}});

  ASSERT_TRUE(std::holds_alternative<TableHeapError>(result));
  EXPECT_EQ(std::get<TableHeapError>(result).code,
            TableHeapErrorCode::InvalidRowId);
}

}  // namespace
}  // namespace curiodb::storage

