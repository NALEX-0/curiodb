#include "curiodb/storage/disk_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

#include "curiodb/storage/slotted_page.hpp"

namespace curiodb::storage {
namespace {

DiskError error(DiskErrorCode code, std::string message) {
  return {code, std::move(message)};
}

}  // namespace

DiskManager::DiskManager(std::filesystem::path path) : path_(std::move(path)) {}

DiskManager::~DiskManager() {
  if (file_.is_open()) {
    file_.flush();
  }
}

const std::filesystem::path& DiskManager::path() const noexcept { return path_; }

std::size_t DiskManager::page_count() const noexcept { return page_count_; }

PageAllocationResult DiskManager::allocate_page() {
  if (page_count_ > std::numeric_limits<std::uint32_t>::max()) {
    return error(DiskErrorCode::PageIdExhausted,
                 "database has exhausted its page identifiers");
  }

  const PageId page_id{static_cast<std::uint32_t>(page_count_)};
  const PageBytes empty_page{};
  file_.clear();
  file_.seekp(static_cast<std::streamoff>(page_offset(page_id)), std::ios::beg);
  file_.write(reinterpret_cast<const char*>(empty_page.data()),
              static_cast<std::streamsize>(empty_page.size()));
  if (!file_) {
    return error(DiskErrorCode::IoError, "failed to allocate database page");
  }
  ++page_count_;
  return page_id;
}

PageReadResult DiskManager::read_page(PageId page_id) {
  if (static_cast<std::size_t>(page_id.value) >= page_count_) {
    return error(DiskErrorCode::InvalidPageId, "page does not exist");
  }

  PageBytes bytes;
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(page_offset(page_id)), std::ios::beg);
  file_.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!file_ || file_.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return error(DiskErrorCode::IoError, "failed to read complete database page");
  }
  return bytes;
}

DiskResult DiskManager::write_page(
    PageId page_id, std::span<const std::byte, kPageSize> bytes) {
  if (static_cast<std::size_t>(page_id.value) >= page_count_) {
    return error(DiskErrorCode::InvalidPageId, "page does not exist");
  }

  file_.clear();
  file_.seekp(static_cast<std::streamoff>(page_offset(page_id)), std::ios::beg);
  file_.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!file_) {
    return error(DiskErrorCode::IoError, "failed to write database page");
  }
  return std::monostate{};
}

DiskResult DiskManager::flush() {
  file_.flush();
  if (!file_) {
    return error(DiskErrorCode::IoError, "failed to flush database file");
  }
  return std::monostate{};
}

std::uint64_t DiskManager::page_offset(PageId page_id) const noexcept {
  return static_cast<std::uint64_t>(page_id.value) * kPageSize;
}

DiskOpenResult open_disk_manager(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    std::ofstream created{path, std::ios::binary};
    if (!created) {
      return error(DiskErrorCode::OpenFailed,
                   "failed to create database file");
    }
  }

  std::error_code filesystem_error;
  const std::uintmax_t size = std::filesystem::file_size(path, filesystem_error);
  if (filesystem_error) {
    return error(DiskErrorCode::OpenFailed,
                 "failed to inspect database file");
  }
  if (size % kPageSize != 0) {
    return error(DiskErrorCode::MisalignedFile,
                 "database file size is not a multiple of page size");
  }

  auto manager = std::unique_ptr<DiskManager>(new DiskManager{path});
  manager->file_.open(path, std::ios::binary | std::ios::in | std::ios::out);
  if (!manager->file_) {
    return error(DiskErrorCode::OpenFailed, "failed to open database file");
  }
  manager->page_count_ = static_cast<std::size_t>(size / kPageSize);
  return manager;
}

}  // namespace curiodb::storage

