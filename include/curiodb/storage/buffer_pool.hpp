#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "curiodb/storage/disk_manager.hpp"

namespace curiodb::storage {

enum class BufferPoolErrorCode {
  DiskError,
  NoEvictableFrame,
};

struct BufferPoolError {
  BufferPoolErrorCode code;
  std::string message;
};

class BufferPool;

class PageGuard {
 public:
  PageGuard() = default;
  PageGuard(const PageGuard&) = delete;
  PageGuard& operator=(const PageGuard&) = delete;
  PageGuard(PageGuard&& other) noexcept;
  PageGuard& operator=(PageGuard&& other) noexcept;
  ~PageGuard();

  [[nodiscard]] PageId page_id() const noexcept;
  [[nodiscard]] std::span<std::byte, kPageSize> bytes() noexcept;
  [[nodiscard]] std::span<const std::byte, kPageSize> bytes() const noexcept;
  void mark_dirty() noexcept;

 private:
  PageGuard(BufferPool* pool, PageId page_id, PageBytes* bytes);
  void release() noexcept;

  BufferPool* pool_{nullptr};
  PageId page_id_{};
  PageBytes* bytes_{nullptr};
  bool dirty_{false};

  friend class BufferPool;
};

using PageGuardResult = std::variant<PageGuard, BufferPoolError>;

class BufferPool {
 public:
  BufferPool(DiskManager& disk_manager, std::size_t capacity);
  BufferPool(const BufferPool&) = delete;
  BufferPool& operator=(const BufferPool&) = delete;
  ~BufferPool();

  [[nodiscard]] std::size_t capacity() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] PageGuardResult fetch_page(PageId page_id);
  [[nodiscard]] PageGuardResult allocate_page();
  [[nodiscard]] DiskResult flush_page(PageId page_id);
  [[nodiscard]] DiskResult flush_all();

 private:
  struct Frame {
    PageId page_id;
    PageBytes bytes{};
    std::size_t pin_count{0};
    std::size_t last_used{0};
    bool dirty{false};
  };

  [[nodiscard]] std::variant<Frame*, BufferPoolError> load(PageId page_id);
  void unpin(PageId page_id, bool dirty) noexcept;

  DiskManager& disk_manager_;
  std::size_t capacity_;
  std::size_t clock_{0};
  std::unordered_map<std::uint32_t, std::unique_ptr<Frame>> frames_;

  friend class PageGuard;
};

}  // namespace curiodb::storage
