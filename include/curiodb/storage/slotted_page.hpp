#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <variant>

namespace curiodb::storage {

inline constexpr std::size_t kPageSize = 4096;
using PageBytes = std::array<std::byte, kPageSize>;

struct SlotId {
  std::uint16_t value{0};

  [[nodiscard]] friend bool operator==(const SlotId&, const SlotId&) = default;
};

enum class PageErrorCode {
  PageFull,
  RecordTooLarge,
  InvalidSlot,
  DeletedRecord,
  CorruptPage,
  UnsupportedVersion,
};

struct PageError {
  PageErrorCode code;
  std::string message;

  [[nodiscard]] friend bool operator==(
      const PageError&, const PageError&) = default;
};

using InsertRecordResult = std::variant<SlotId, PageError>;
using ReadRecordResult =
    std::variant<std::span<const std::byte>, PageError>;
using DeleteRecordResult = std::variant<std::monostate, PageError>;
using UpdateRecordResult = std::variant<std::monostate, PageError>;

class SlottedPage {
 public:
  SlottedPage();

  [[nodiscard]] std::size_t slot_count() const noexcept;
  [[nodiscard]] std::size_t free_space() const noexcept;
  [[nodiscard]] std::span<const std::byte, kPageSize> bytes() const noexcept;

  [[nodiscard]] InsertRecordResult insert_record(
      std::span<const std::byte> record);
  [[nodiscard]] ReadRecordResult read_record(SlotId slot) const;
  [[nodiscard]] DeleteRecordResult delete_record(SlotId slot);
  [[nodiscard]] UpdateRecordResult update_record(
      SlotId slot, std::span<const std::byte> record);

 private:
  struct LoadedPageTag {};
  SlottedPage(PageBytes bytes, LoadedPageTag);

  PageBytes bytes_{};

  friend std::variant<std::unique_ptr<SlottedPage>, PageError>
  load_slotted_page(PageBytes bytes);
};

using PageLoadResult =
    std::variant<std::unique_ptr<SlottedPage>, PageError>;

[[nodiscard]] PageLoadResult load_slotted_page(PageBytes bytes);

}  // namespace curiodb::storage
