#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "curiodb/storage/buffer_pool.hpp"
#include "curiodb/storage/table_heap.hpp"

namespace curiodb::storage {

enum class BPlusTreeErrorCode {
  BufferPoolError,
  CorruptNode,
  DuplicateKey,
  InvalidConfiguration,
};

struct BPlusTreeError {
  BPlusTreeErrorCode code;
  std::string message;
};

using BPlusTreeResult = std::variant<std::monostate, BPlusTreeError>;
using BPlusTreeLookupResult =
    std::variant<std::optional<RowId>, BPlusTreeError>;
using BPlusTreeRangeResult = std::variant<std::vector<RowId>, BPlusTreeError>;

class BPlusTree {
 public:
  static constexpr std::size_t kDefaultMaxKeys = 200;

  explicit BPlusTree(BufferPool& buffer_pool,
                     std::optional<PageId> root_page_id = std::nullopt,
                     std::size_t max_keys = kDefaultMaxKeys);

  [[nodiscard]] std::optional<PageId> root_page_id() const noexcept;
  [[nodiscard]] BPlusTreeResult insert(std::int64_t key, RowId row_id);
  [[nodiscard]] BPlusTreeResult erase(std::int64_t key);
  [[nodiscard]] BPlusTreeLookupResult find(std::int64_t key);
  [[nodiscard]] BPlusTreeRangeResult range(std::optional<std::int64_t> lower,
                                           std::optional<std::int64_t> upper);
  [[nodiscard]] DiskResult flush();

 private:
  struct Node;
  struct Split;

  [[nodiscard]] std::variant<Node, BPlusTreeError> read_node(PageId page_id);
  [[nodiscard]] BPlusTreeResult write_node(const Node& node);
  [[nodiscard]] std::variant<std::optional<Split>, BPlusTreeError>
  insert_recursive(PageId page_id, std::int64_t key, RowId row_id);

  BufferPool& buffer_pool_;
  std::optional<PageId> root_page_id_;
  std::size_t max_keys_;
};

}  // namespace curiodb::storage
