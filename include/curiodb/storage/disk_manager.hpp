#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <variant>

#include "curiodb/storage/slotted_page.hpp"

namespace curiodb::storage {

struct PageId {
  std::uint32_t value{0};

  [[nodiscard]] friend bool operator==(const PageId&, const PageId&) = default;
};

enum class DiskErrorCode {
  OpenFailed,
  IoError,
  InvalidPageId,
  MisalignedFile,
  PageIdExhausted,
};

struct DiskError {
  DiskErrorCode code;
  std::string message;

  [[nodiscard]] friend bool operator==(
      const DiskError&, const DiskError&) = default;
};

using DiskResult = std::variant<std::monostate, DiskError>;
using PageAllocationResult = std::variant<PageId, DiskError>;
using PageReadResult = std::variant<PageBytes, DiskError>;

class DiskManager {
 public:
  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;
  DiskManager(DiskManager&&) = delete;
  DiskManager& operator=(DiskManager&&) = delete;
  ~DiskManager();

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] std::size_t page_count() const noexcept;

  [[nodiscard]] PageAllocationResult allocate_page();
  [[nodiscard]] PageReadResult read_page(PageId page_id);
  [[nodiscard]] DiskResult write_page(
      PageId page_id, std::span<const std::byte, kPageSize> bytes);
  [[nodiscard]] DiskResult flush();

 private:
  explicit DiskManager(std::filesystem::path path);

  [[nodiscard]] std::uint64_t page_offset(PageId page_id) const noexcept;

  std::filesystem::path path_;
  std::fstream file_;
  std::size_t page_count_{0};

  friend std::variant<std::unique_ptr<DiskManager>, DiskError>
  open_disk_manager(const std::filesystem::path& path);
};

using DiskOpenResult =
    std::variant<std::unique_ptr<DiskManager>, DiskError>;

[[nodiscard]] DiskOpenResult open_disk_manager(
    const std::filesystem::path& path);

}  // namespace curiodb::storage

