#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "curiodb/storage/row.hpp"
#include "curiodb/storage/serialization.hpp"
#include "curiodb/storage/slotted_page.hpp"
#include "curiodb/types/value.hpp"

namespace curiodb::storage {
namespace {

SerializedBytes bytes_of(std::string text) {
  SerializedBytes bytes;
  bytes.reserve(text.size());
  for (const char character : text) {
    bytes.push_back(static_cast<std::byte>(
        static_cast<unsigned char>(character)));
  }
  return bytes;
}

SlotId expect_slot(InsertRecordResult result) {
  EXPECT_TRUE(std::holds_alternative<SlotId>(result));
  return std::get<SlotId>(result);
}

std::span<const std::byte> expect_record(const ReadRecordResult& result) {
  EXPECT_TRUE(std::holds_alternative<std::span<const std::byte>>(result));
  return std::get<std::span<const std::byte>>(result);
}

TEST(SlottedPageTest, StartsEmptyWithUsableSpace) {
  const SlottedPage page;

  EXPECT_EQ(page.slot_count(), 0U);
  EXPECT_EQ(page.free_space(), kPageSize - 16U);
  EXPECT_EQ(page.bytes().size(), kPageSize);
}

TEST(SlottedPageTest, InsertsVariableRecordsWithStableSlots) {
  SlottedPage page;
  const auto alice = bytes_of("Alice");
  const auto bob = bytes_of("Bob the builder");

  const SlotId first = expect_slot(page.insert_record(alice));
  const SlotId second = expect_slot(page.insert_record(bob));

  EXPECT_EQ(first, (SlotId{0}));
  EXPECT_EQ(second, (SlotId{1}));
  EXPECT_EQ(page.slot_count(), 2U);
  EXPECT_TRUE(std::ranges::equal(expect_record(page.read_record(first)), alice));
  EXPECT_TRUE(std::ranges::equal(expect_record(page.read_record(second)), bob));
}

TEST(SlottedPageTest, RejectsInvalidSlot) {
  const SlottedPage page;

  const auto result = page.read_record(SlotId{0});

  ASSERT_TRUE(std::holds_alternative<PageError>(result));
  EXPECT_EQ(std::get<PageError>(result).code, PageErrorCode::InvalidSlot);
}

TEST(SlottedPageTest, ReportsFullWithoutChangingExistingRecords) {
  SlottedPage page;
  const auto first_record = bytes_of("kept");
  const SlotId first = expect_slot(page.insert_record(first_record));
  const SerializedBytes too_large(page.free_space(), std::byte{1});

  const auto result = page.insert_record(too_large);

  ASSERT_TRUE(std::holds_alternative<PageError>(result));
  EXPECT_EQ(std::get<PageError>(result).code, PageErrorCode::PageFull);
  EXPECT_EQ(page.slot_count(), 1U);
  EXPECT_TRUE(
      std::ranges::equal(expect_record(page.read_record(first)), first_record));
}

TEST(SlottedPageTest, FillsPageWithManySmallRecords) {
  SlottedPage page;
  const SerializedBytes record(20, std::byte{7});
  std::size_t inserted = 0;
  while (std::holds_alternative<SlotId>(page.insert_record(record))) {
    ++inserted;
  }

  EXPECT_GT(inserted, 100U);
  EXPECT_LT(page.free_space(), record.size() + 4U);
}

TEST(SlottedPageTest, ReloadsIdenticalPageImage) {
  SlottedPage original;
  const auto record = bytes_of("persistent record");
  const SlotId slot = expect_slot(original.insert_record(record));
  PageBytes image;
  std::ranges::copy(original.bytes(), image.begin());

  auto loaded = load_slotted_page(image);

  ASSERT_TRUE(
      std::holds_alternative<std::unique_ptr<SlottedPage>>(loaded));
  const auto& page = *std::get<std::unique_ptr<SlottedPage>>(loaded);
  EXPECT_EQ(page.slot_count(), 1U);
  EXPECT_TRUE(std::ranges::equal(expect_record(page.read_record(slot)), record));
  EXPECT_TRUE(std::ranges::equal(page.bytes(), original.bytes()));
}

TEST(SlottedPageTest, RejectsCorruptMagicAndVersion) {
  SlottedPage original;
  PageBytes image;
  std::ranges::copy(original.bytes(), image.begin());
  image[0] = std::byte{0};
  auto bad_magic = load_slotted_page(image);
  ASSERT_TRUE(std::holds_alternative<PageError>(bad_magic));
  EXPECT_EQ(std::get<PageError>(bad_magic).code, PageErrorCode::CorruptPage);

  std::ranges::copy(original.bytes(), image.begin());
  image[4] = std::byte{2};
  auto bad_version = load_slotted_page(image);
  ASSERT_TRUE(std::holds_alternative<PageError>(bad_version));
  EXPECT_EQ(std::get<PageError>(bad_version).code,
            PageErrorCode::UnsupportedVersion);
}

TEST(SlottedPageTest, StoresAndRestoresSerializedRow) {
  const Row original{Value{std::int64_t{1}}, Value{"Alice"}, Value{65000.5}};
  const auto serialized = serialize_row(original);
  ASSERT_TRUE(std::holds_alternative<SerializedBytes>(serialized));
  SlottedPage page;
  const SlotId slot = expect_slot(
      page.insert_record(std::get<SerializedBytes>(serialized)));

  const auto decoded = deserialize_row(expect_record(page.read_record(slot)));

  ASSERT_TRUE(std::holds_alternative<Row>(decoded));
  EXPECT_EQ(std::get<Row>(decoded), original);
}

TEST(SlottedPageTest, TombstonesDeletedRecordWithoutChangingOtherSlots) {
  SlottedPage page;
  const std::array first{std::byte{1}, std::byte{2}};
  const std::array second{std::byte{3}};
  const SlotId first_slot = std::get<SlotId>(page.insert_record(first));
  const SlotId second_slot = std::get<SlotId>(page.insert_record(second));

  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      page.delete_record(first_slot)));
  const auto deleted = page.read_record(first_slot);
  ASSERT_TRUE(std::holds_alternative<PageError>(deleted));
  EXPECT_EQ(std::get<PageError>(deleted).code, PageErrorCode::DeletedRecord);
  const auto retained = page.read_record(second_slot);
  ASSERT_TRUE(std::holds_alternative<std::span<const std::byte>>(retained));
  EXPECT_EQ(std::get<std::span<const std::byte>>(retained).size(), 1U);
}

}  // namespace
}  // namespace curiodb::storage
