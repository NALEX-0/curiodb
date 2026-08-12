#pragma once

#include <span>
#include <string>
#include <variant>
#include <vector>

#include "curiodb/storage/disk_manager.hpp"
#include "curiodb/storage/row.hpp"
#include "curiodb/storage/slotted_page.hpp"

namespace curiodb::storage {

struct RowId {
  PageId page_id;
  SlotId slot_id;

  [[nodiscard]] friend bool operator==(const RowId&, const RowId&) = default;
};

struct HeapRow {
  RowId id;
  Row row;

  [[nodiscard]] friend bool operator==(
      const HeapRow&, const HeapRow&) = default;
};

enum class TableHeapErrorCode {
  DiskError,
  CorruptPage,
  SerializationError,
  RowTooLarge,
  InvalidRowId,
};

struct TableHeapError {
  TableHeapErrorCode code;
  std::string message;

  [[nodiscard]] friend bool operator==(
      const TableHeapError&, const TableHeapError&) = default;
};

using HeapInsertResult = std::variant<RowId, TableHeapError>;
using HeapFetchResult = std::variant<Row, TableHeapError>;
using HeapScanResult = std::variant<std::vector<HeapRow>, TableHeapError>;
using HeapDeleteResult = std::variant<std::monostate, TableHeapError>;

class TableHeap {
 public:
  explicit TableHeap(DiskManager& disk_manager,
                     std::vector<PageId> page_ids = {});

  [[nodiscard]] std::span<const PageId> page_ids() const noexcept;
  [[nodiscard]] HeapInsertResult insert(const Row& row);
  [[nodiscard]] HeapFetchResult fetch(RowId row_id);
  [[nodiscard]] HeapScanResult scan();
  [[nodiscard]] HeapDeleteResult delete_row(RowId row_id);
  [[nodiscard]] DiskResult flush();

 private:
  [[nodiscard]] std::variant<std::unique_ptr<SlottedPage>, TableHeapError>
  load_page(PageId page_id);

  DiskManager& disk_manager_;
  std::vector<PageId> page_ids_;
};

}  // namespace curiodb::storage
