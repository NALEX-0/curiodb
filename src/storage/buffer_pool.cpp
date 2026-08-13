#include "curiodb/storage/buffer_pool.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>

namespace curiodb::storage {
namespace {

BufferPoolError buffer_error(BufferPoolErrorCode code, std::string message) {
  return {code, std::move(message)};
}

BufferPoolError disk_error(const DiskError& error) {
  return buffer_error(BufferPoolErrorCode::DiskError, error.message);
}

}  // namespace

PageGuard::PageGuard(BufferPool* pool, PageId page_id, PageBytes* bytes)
    : pool_(pool), page_id_(page_id), bytes_(bytes) {}

PageGuard::PageGuard(PageGuard&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      page_id_(other.page_id_),
      bytes_(std::exchange(other.bytes_, nullptr)),
      dirty_(std::exchange(other.dirty_, false)) {}

PageGuard& PageGuard::operator=(PageGuard&& other) noexcept {
  if (this != &other) {
    release();
    pool_ = std::exchange(other.pool_, nullptr);
    page_id_ = other.page_id_;
    bytes_ = std::exchange(other.bytes_, nullptr);
    dirty_ = std::exchange(other.dirty_, false);
  }
  return *this;
}

PageGuard::~PageGuard() { release(); }

PageId PageGuard::page_id() const noexcept { return page_id_; }

std::span<std::byte, kPageSize> PageGuard::bytes() noexcept { return *bytes_; }

std::span<const std::byte, kPageSize> PageGuard::bytes() const noexcept {
  return *bytes_;
}

void PageGuard::mark_dirty() noexcept {
  dirty_ = true;
}

void PageGuard::release() noexcept {
  if (pool_ != nullptr) {
    pool_->unpin(page_id_, dirty_);
    pool_ = nullptr;
    bytes_ = nullptr;
    dirty_ = false;
  }
}

BufferPool::BufferPool(DiskManager& disk_manager, std::size_t capacity)
    : disk_manager_(disk_manager), capacity_(capacity) {}

BufferPool::~BufferPool() { static_cast<void>(flush_all()); }

std::size_t BufferPool::capacity() const noexcept { return capacity_; }

std::size_t BufferPool::size() const noexcept { return frames_.size(); }

PageGuardResult BufferPool::fetch_page(PageId page_id) {
  auto loaded = load(page_id);
  if (const auto* error = std::get_if<BufferPoolError>(&loaded)) {
    return *error;
  }
  Frame* const frame = std::get<Frame*>(loaded);
  ++frame->pin_count;
  frame->last_used = ++clock_;
  return PageGuard{this, page_id, &frame->bytes};
}

PageGuardResult BufferPool::allocate_page() {
  const auto allocation = disk_manager_.allocate_page();
  if (const auto* error = std::get_if<DiskError>(&allocation)) {
    return disk_error(*error);
  }
  return fetch_page(std::get<PageId>(allocation));
}

DiskResult BufferPool::flush_page(PageId page_id) {
  const auto found = frames_.find(page_id.value);
  if (found == frames_.end() || !found->second->dirty) {
    return std::monostate{};
  }
  const auto written = disk_manager_.write_page(page_id, found->second->bytes);
  if (std::holds_alternative<DiskError>(written)) {
    return std::get<DiskError>(written);
  }
  found->second->dirty = false;
  return std::monostate{};
}

DiskResult BufferPool::flush_all() {
  for (const auto& [key, frame] : frames_) {
    static_cast<void>(key);
    const auto flushed = flush_page(frame->page_id);
    if (std::holds_alternative<DiskError>(flushed)) {
      return std::get<DiskError>(flushed);
    }
  }
  return disk_manager_.flush();
}

std::variant<BufferPool::Frame*, BufferPoolError> BufferPool::load(
    PageId page_id) {
  if (const auto found = frames_.find(page_id.value); found != frames_.end()) {
    return found->second.get();
  }
  if (capacity_ == 0) {
    return buffer_error(BufferPoolErrorCode::NoEvictableFrame,
                        "buffer pool has zero capacity");
  }
  if (frames_.size() >= capacity_) {
    auto victim = frames_.end();
    std::size_t oldest = std::numeric_limits<std::size_t>::max();
    for (auto candidate = frames_.begin(); candidate != frames_.end();
         ++candidate) {
      if (candidate->second->pin_count == 0 &&
          candidate->second->last_used < oldest) {
        victim = candidate;
        oldest = candidate->second->last_used;
      }
    }
    if (victim == frames_.end()) {
      return buffer_error(BufferPoolErrorCode::NoEvictableFrame,
                          "all buffer pool frames are pinned");
    }
    const auto flushed = flush_page(victim->second->page_id);
    if (const auto* error = std::get_if<DiskError>(&flushed)) {
      return disk_error(*error);
    }
    frames_.erase(victim);
  }
  auto read = disk_manager_.read_page(page_id);
  if (const auto* error = std::get_if<DiskError>(&read)) {
    return disk_error(*error);
  }
  auto frame = std::make_unique<Frame>();
  frame->page_id = page_id;
  frame->bytes = std::get<PageBytes>(std::move(read));
  Frame* const pointer = frame.get();
  frames_.emplace(page_id.value, std::move(frame));
  return pointer;
}

void BufferPool::unpin(PageId page_id, bool dirty) noexcept {
  const auto found = frames_.find(page_id.value);
  if (found == frames_.end()) {
    return;
  }
  if (found->second->pin_count > 0) {
    --found->second->pin_count;
  }
  found->second->dirty = found->second->dirty || dirty;
  found->second->last_used = ++clock_;
}

}  // namespace curiodb::storage
