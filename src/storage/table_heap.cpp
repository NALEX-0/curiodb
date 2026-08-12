#include "curiodb/storage/table_heap.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "curiodb/storage/disk_manager.hpp"
#include "curiodb/storage/row.hpp"
#include "curiodb/storage/serialization.hpp"
#include "curiodb/storage/slotted_page.hpp"

namespace curiodb::storage {
namespace {

TableHeapError heap_error(TableHeapErrorCode code, std::string message) {
  return {code, std::move(message)};
}

TableHeapError from_disk_error(const DiskError& error) {
  return heap_error(TableHeapErrorCode::DiskError, error.message);
}

TableHeapError from_page_error(const PageError& error) {
  return heap_error(TableHeapErrorCode::CorruptPage, error.message);
}

TableHeapError from_serialization_error(const SerializationError& error) {
  return heap_error(TableHeapErrorCode::SerializationError, error.message);
}

}  // namespace

TableHeap::TableHeap(DiskManager& disk_manager, std::vector<PageId> page_ids)
    : disk_manager_(disk_manager), page_ids_(std::move(page_ids)) {}

std::span<const PageId> TableHeap::page_ids() const noexcept {
  return page_ids_;
}

HeapInsertResult TableHeap::insert(const Row& row) {
  auto serialization = serialize_row(row);
  if (const auto* error = std::get_if<SerializationError>(&serialization)) {
    return from_serialization_error(*error);
  }
  const auto& record = std::get<SerializedBytes>(serialization);

  SlottedPage empty_page;
  if (record.size() + 4U > empty_page.free_space()) {
    return heap_error(TableHeapErrorCode::RowTooLarge,
                      "serialized row does not fit in a page");
  }

  if (!page_ids_.empty()) {
    const PageId page_id = page_ids_.back();
    auto loaded = load_page(page_id);
    if (const auto* error = std::get_if<TableHeapError>(&loaded)) {
      return *error;
    }
    auto page = std::move(std::get<std::unique_ptr<SlottedPage>>(loaded));
    auto inserted = page->insert_record(record);
    if (const auto* slot = std::get_if<SlotId>(&inserted)) {
      auto write = disk_manager_.write_page(page_id, page->bytes());
      if (const auto* error = std::get_if<DiskError>(&write)) {
        return from_disk_error(*error);
      }
      return RowId{page_id, *slot};
    }
    const auto& page_error = std::get<PageError>(inserted);
    if (page_error.code != PageErrorCode::PageFull) {
      return from_page_error(page_error);
    }
  }

  auto allocation = disk_manager_.allocate_page();
  if (const auto* error = std::get_if<DiskError>(&allocation)) {
    return from_disk_error(*error);
  }
  const PageId page_id = std::get<PageId>(allocation);
  auto inserted = empty_page.insert_record(record);
  if (const auto* error = std::get_if<PageError>(&inserted)) {
    return from_page_error(*error);
  }
  const SlotId slot = std::get<SlotId>(inserted);
  auto write = disk_manager_.write_page(page_id, empty_page.bytes());
  if (const auto* error = std::get_if<DiskError>(&write)) {
    return from_disk_error(*error);
  }
  page_ids_.push_back(page_id);
  return RowId{page_id, slot};
}

HeapFetchResult TableHeap::fetch(RowId row_id) {
  if (std::find(page_ids_.begin(), page_ids_.end(), row_id.page_id) ==
      page_ids_.end()) {
    return heap_error(TableHeapErrorCode::InvalidRowId,
                      "row page does not belong to this table");
  }
  auto loaded = load_page(row_id.page_id);
  if (const auto* error = std::get_if<TableHeapError>(&loaded)) {
    return *error;
  }
  const auto& page = *std::get<std::unique_ptr<SlottedPage>>(loaded);
  auto record = page.read_record(row_id.slot_id);
  if (const auto* error = std::get_if<PageError>(&record)) {
    return heap_error(TableHeapErrorCode::InvalidRowId, error->message);
  }
  auto row = deserialize_row(
      std::get<std::span<const std::byte>>(record));
  if (const auto* error = std::get_if<SerializationError>(&row)) {
    return from_serialization_error(*error);
  }
  return std::get<Row>(std::move(row));
}

HeapScanResult TableHeap::scan() {
  std::vector<HeapRow> rows;
  for (const PageId page_id : page_ids_) {
    auto loaded = load_page(page_id);
    if (const auto* error = std::get_if<TableHeapError>(&loaded)) {
      return *error;
    }
    const auto& page = *std::get<std::unique_ptr<SlottedPage>>(loaded);
    for (std::size_t index = 0; index < page.slot_count(); ++index) {
      const SlotId slot_id{static_cast<std::uint16_t>(index)};
      auto record = page.read_record(slot_id);
      if (const auto* error = std::get_if<PageError>(&record)) {
        if (error->code == PageErrorCode::DeletedRecord) {
          continue;
        }
        return from_page_error(*error);
      }
      auto row = deserialize_row(
          std::get<std::span<const std::byte>>(record));
      if (const auto* error = std::get_if<SerializationError>(&row)) {
        return from_serialization_error(*error);
      }
      rows.push_back(
          {.id = {page_id, slot_id},
           .row = std::get<Row>(std::move(row))});
    }
  }
  return rows;
}

HeapDeleteResult TableHeap::delete_row(RowId row_id) {
  if (std::find(page_ids_.begin(), page_ids_.end(), row_id.page_id) ==
      page_ids_.end()) {
    return heap_error(TableHeapErrorCode::InvalidRowId,
                      "row page does not belong to this table");
  }
  auto loaded = load_page(row_id.page_id);
  if (const auto* error = std::get_if<TableHeapError>(&loaded)) {
    return *error;
  }
  auto page = std::move(std::get<std::unique_ptr<SlottedPage>>(loaded));
  const auto deleted = page->delete_record(row_id.slot_id);
  if (const auto* error = std::get_if<PageError>(&deleted)) {
    return heap_error(TableHeapErrorCode::InvalidRowId, error->message);
  }
  const auto written = disk_manager_.write_page(row_id.page_id, page->bytes());
  if (const auto* error = std::get_if<DiskError>(&written)) {
    return from_disk_error(*error);
  }
  return std::monostate{};
}

HeapUpdateResult TableHeap::update(RowId row_id, const Row& row) {
  if (std::find(page_ids_.begin(), page_ids_.end(), row_id.page_id) ==
      page_ids_.end()) {
    return heap_error(TableHeapErrorCode::InvalidRowId,
                      "row page does not belong to this table");
  }
  auto serialization = serialize_row(row);
  if (const auto* error = std::get_if<SerializationError>(&serialization)) {
    return from_serialization_error(*error);
  }
  auto loaded = load_page(row_id.page_id);
  if (const auto* error = std::get_if<TableHeapError>(&loaded)) {
    return *error;
  }
  auto page = std::move(std::get<std::unique_ptr<SlottedPage>>(loaded));
  const auto replaced = page->update_record(
      row_id.slot_id, std::get<SerializedBytes>(serialization));
  if (std::holds_alternative<std::monostate>(replaced)) {
    const auto written =
        disk_manager_.write_page(row_id.page_id, page->bytes());
    if (const auto* error = std::get_if<DiskError>(&written)) {
      return from_disk_error(*error);
    }
    return row_id;
  }
  const auto& page_error = std::get<PageError>(replaced);
  if (page_error.code != PageErrorCode::PageFull) {
    return heap_error(TableHeapErrorCode::InvalidRowId, page_error.message);
  }
  auto inserted = insert(row);
  if (const auto* error = std::get_if<TableHeapError>(&inserted)) {
    return *error;
  }
  const RowId new_id = std::get<RowId>(inserted);
  const auto deleted = delete_row(row_id);
  if (const auto* error = std::get_if<TableHeapError>(&deleted)) {
    return *error;
  }
  return new_id;
}

DiskResult TableHeap::flush() { return disk_manager_.flush(); }

std::variant<std::unique_ptr<SlottedPage>, TableHeapError>
TableHeap::load_page(PageId page_id) {
  auto read = disk_manager_.read_page(page_id);
  if (const auto* error = std::get_if<DiskError>(&read)) {
    return from_disk_error(*error);
  }
  auto loaded = load_slotted_page(std::get<PageBytes>(std::move(read)));
  if (const auto* error = std::get_if<PageError>(&loaded)) {
    return from_page_error(*error);
  }
  return std::move(std::get<std::unique_ptr<SlottedPage>>(loaded));
}

}  // namespace curiodb::storage
