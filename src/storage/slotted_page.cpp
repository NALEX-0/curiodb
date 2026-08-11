#include "curiodb/storage/slotted_page.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace curiodb::storage {
namespace {

constexpr std::uint32_t kMagic = 0x49525543;  // "CURI" in little endian
constexpr std::uint16_t kFormatVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kSlotSize = 4;
constexpr std::size_t kMagicOffset = 0;
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kSlotCountOffset = 6;
constexpr std::size_t kFreeStartOffset = 8;
constexpr std::size_t kFreeEndOffset = 10;

std::uint16_t read_u16(const PageBytes& bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(
      std::to_integer<std::uint8_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(
           std::to_integer<std::uint8_t>(bytes[offset + 1]))
       << 8));
}

std::uint32_t read_u32(const PageBytes& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<std::uint8_t>(bytes[offset + shift / 8]))
             << shift;
  }
  return value;
}

void write_u16(PageBytes& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::byte>((value >> 8) & 0xffU);
}

void write_u32(PageBytes& bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    bytes[offset + shift / 8] =
        static_cast<std::byte>((value >> shift) & 0xffU);
  }
}

std::size_t slot_offset(SlotId slot) {
  return kHeaderSize + static_cast<std::size_t>(slot.value) * kSlotSize;
}

PageError corrupt(std::string message) {
  return {PageErrorCode::CorruptPage, std::move(message)};
}

std::variant<std::monostate, PageError> validate(const PageBytes& bytes) {
  if (read_u32(bytes, kMagicOffset) != kMagic) {
    return corrupt("invalid page magic");
  }
  if (read_u16(bytes, kVersionOffset) != kFormatVersion) {
    return PageError{PageErrorCode::UnsupportedVersion,
                     "unsupported page format version"};
  }

  const std::size_t count = read_u16(bytes, kSlotCountOffset);
  const std::size_t free_start = read_u16(bytes, kFreeStartOffset);
  const std::size_t free_end = read_u16(bytes, kFreeEndOffset);
  if (free_start != kHeaderSize + count * kSlotSize ||
      free_start > free_end || free_end > kPageSize) {
    return corrupt("invalid page free-space boundaries");
  }

  struct Range {
    std::size_t begin;
    std::size_t end;
  };
  std::vector<Range> ranges;
  ranges.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const SlotId slot{static_cast<std::uint16_t>(index)};
    const std::size_t entry = slot_offset(slot);
    const std::size_t record_offset = read_u16(bytes, entry);
    const std::size_t record_length = read_u16(bytes, entry + 2);
    if (record_offset < free_end || record_offset > kPageSize ||
        record_length > kPageSize - record_offset) {
      return corrupt("slot points outside the record area");
    }
    if (record_length != 0) {
      ranges.push_back({record_offset, record_offset + record_length});
    }
  }
  std::sort(ranges.begin(), ranges.end(),
            [](const Range& left, const Range& right) {
              return left.begin < right.begin;
            });
  for (std::size_t index = 1; index < ranges.size(); ++index) {
    if (ranges[index - 1].end > ranges[index].begin) {
      return corrupt("record ranges overlap");
    }
  }
  return std::monostate{};
}

}  // namespace

SlottedPage::SlottedPage() {
  write_u32(bytes_, kMagicOffset, kMagic);
  write_u16(bytes_, kVersionOffset, kFormatVersion);
  write_u16(bytes_, kSlotCountOffset, 0);
  write_u16(bytes_, kFreeStartOffset,
            static_cast<std::uint16_t>(kHeaderSize));
  write_u16(bytes_, kFreeEndOffset, static_cast<std::uint16_t>(kPageSize));
}

SlottedPage::SlottedPage(PageBytes bytes, LoadedPageTag)
    : bytes_(std::move(bytes)) {}

std::size_t SlottedPage::slot_count() const noexcept {
  return read_u16(bytes_, kSlotCountOffset);
}

std::size_t SlottedPage::free_space() const noexcept {
  return static_cast<std::size_t>(read_u16(bytes_, kFreeEndOffset)) -
         read_u16(bytes_, kFreeStartOffset);
}

std::span<const std::byte, kPageSize> SlottedPage::bytes() const noexcept {
  return bytes_;
}

InsertRecordResult SlottedPage::insert_record(
    std::span<const std::byte> record) {
  if (record.size() > std::numeric_limits<std::uint16_t>::max()) {
    return PageError{PageErrorCode::RecordTooLarge,
                     "record exceeds slot length limit"};
  }
  if (record.size() + kSlotSize > free_space()) {
    return PageError{PageErrorCode::PageFull,
                     "page does not have enough free space"};
  }

  const auto count = read_u16(bytes_, kSlotCountOffset);
  if (count == std::numeric_limits<std::uint16_t>::max()) {
    return PageError{PageErrorCode::PageFull, "page slot directory is full"};
  }
  const SlotId slot{count};
  const auto record_offset = static_cast<std::uint16_t>(
      read_u16(bytes_, kFreeEndOffset) - record.size());
  std::copy(record.begin(), record.end(), bytes_.begin() + record_offset);

  const std::size_t entry = slot_offset(slot);
  write_u16(bytes_, entry, record_offset);
  write_u16(bytes_, entry + 2,
            static_cast<std::uint16_t>(record.size()));
  write_u16(bytes_, kSlotCountOffset,
            static_cast<std::uint16_t>(count + 1));
  write_u16(bytes_, kFreeStartOffset,
            static_cast<std::uint16_t>(entry + kSlotSize));
  write_u16(bytes_, kFreeEndOffset, record_offset);
  return slot;
}

ReadRecordResult SlottedPage::read_record(SlotId slot) const {
  if (slot.value >= slot_count()) {
    return PageError{PageErrorCode::InvalidSlot, "slot does not exist"};
  }
  const std::size_t entry = slot_offset(slot);
  const std::size_t offset = read_u16(bytes_, entry);
  const std::size_t length = read_u16(bytes_, entry + 2);
  return std::span<const std::byte>{bytes_.data() + offset, length};
}

PageLoadResult load_slotted_page(PageBytes bytes) {
  auto validation = validate(bytes);
  if (const auto* error = std::get_if<PageError>(&validation)) {
    return *error;
  }
  return std::unique_ptr<SlottedPage>(
      new SlottedPage{std::move(bytes), SlottedPage::LoadedPageTag{}});
}

}  // namespace curiodb::storage

