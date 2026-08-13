#include "curiodb/storage/b_plus_tree.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace curiodb::storage {
namespace {

constexpr std::uint32_t kMagic = 0x54504243;  // "CBPT"
constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kLeafEntrySize = 16;
constexpr std::size_t kInternalEntrySize = 12;
constexpr std::uint32_t kNoPage = std::numeric_limits<std::uint32_t>::max();

BPlusTreeError error(BPlusTreeErrorCode code, std::string message) {
  return {code, std::move(message)};
}

BPlusTreeError buffer_error(const BufferPoolError& source) {
  return error(BPlusTreeErrorCode::BufferPoolError, source.message);
}

void write_u16(std::span<std::byte> bytes, std::size_t offset,
               std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xffU);
  bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void write_u32(std::span<std::byte> bytes, std::size_t offset,
               std::uint32_t value) {
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    bytes[offset + shift / 8U] =
        static_cast<std::byte>((value >> shift) & 0xffU);
  }
}

void write_u64(std::span<std::byte> bytes, std::size_t offset,
               std::uint64_t value) {
  for (unsigned int shift = 0; shift < 64; shift += 8) {
    bytes[offset + shift / 8U] =
        static_cast<std::byte>((value >> shift) & 0xffU);
  }
}

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(
      std::to_integer<std::uint8_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(
           std::to_integer<std::uint8_t>(bytes[offset + 1]))
       << 8U));
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<std::uint8_t>(bytes[offset + shift / 8U]))
             << shift;
  }
  return value;
}

std::uint64_t read_u64(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (unsigned int shift = 0; shift < 64; shift += 8) {
    value |= static_cast<std::uint64_t>(
                 std::to_integer<std::uint8_t>(bytes[offset + shift / 8U]))
             << shift;
  }
  return value;
}

}  // namespace

struct BPlusTree::Node {
  PageId page_id;
  bool leaf{true};
  std::vector<std::int64_t> keys;
  std::vector<RowId> values;
  std::vector<PageId> children;
  std::optional<PageId> next_leaf;
};

struct BPlusTree::Split {
  std::int64_t separator;
  PageId right_page_id;
};

BPlusTree::BPlusTree(BufferPool& buffer_pool,
                     std::optional<PageId> root_page_id,
                     std::size_t max_keys)
    : buffer_pool_(buffer_pool),
      root_page_id_(root_page_id),
      max_keys_(max_keys) {}

std::optional<PageId> BPlusTree::root_page_id() const noexcept {
  return root_page_id_;
}

BPlusTreeResult BPlusTree::insert(std::int64_t key, RowId row_id) {
  if (max_keys_ < 2 ||
      max_keys_ > (kPageSize - kHeaderSize) / kLeafEntrySize) {
    return error(BPlusTreeErrorCode::InvalidConfiguration,
                 "B+ tree node capacity is invalid");
  }
  if (!root_page_id_.has_value()) {
    auto allocated = buffer_pool_.allocate_page();
    if (const auto* pool_error = std::get_if<BufferPoolError>(&allocated)) {
      return buffer_error(*pool_error);
    }
    auto guard = std::get<PageGuard>(std::move(allocated));
    Node root{.page_id = guard.page_id(),
              .leaf = true,
              .keys = {key},
              .values = {row_id}};
    root_page_id_ = root.page_id;
    guard = PageGuard{};
    return write_node(root);
  }

  auto inserted = insert_recursive(*root_page_id_, key, row_id);
  if (const auto* tree_error = std::get_if<BPlusTreeError>(&inserted)) {
    return *tree_error;
  }
  const auto& split = std::get<std::optional<Split>>(inserted);
  if (!split.has_value()) {
    return std::monostate{};
  }
  auto allocated = buffer_pool_.allocate_page();
  if (const auto* pool_error = std::get_if<BufferPoolError>(&allocated)) {
    return buffer_error(*pool_error);
  }
  auto guard = std::get<PageGuard>(std::move(allocated));
  Node root{.page_id = guard.page_id(),
            .leaf = false,
            .keys = {split->separator},
            .values = {},
            .children = {*root_page_id_, split->right_page_id}};
  guard = PageGuard{};
  const auto written = write_node(root);
  if (std::holds_alternative<BPlusTreeError>(written)) {
    return std::get<BPlusTreeError>(written);
  }
  root_page_id_ = root.page_id;
  return std::monostate{};
}

BPlusTreeLookupResult BPlusTree::find(std::int64_t key) {
  if (!root_page_id_.has_value()) {
    return std::optional<RowId>{};
  }
  PageId current = *root_page_id_;
  while (true) {
    auto loaded = read_node(current);
    if (const auto* tree_error = std::get_if<BPlusTreeError>(&loaded)) {
      return *tree_error;
    }
    const Node& node = std::get<Node>(loaded);
    if (node.leaf) {
      const auto found = std::lower_bound(node.keys.begin(), node.keys.end(), key);
      if (found == node.keys.end() || *found != key) {
        return std::optional<RowId>{};
      }
      return std::optional<RowId>{node.values[static_cast<std::size_t>(
          std::distance(node.keys.begin(), found))]};
    }
    const auto child = std::upper_bound(node.keys.begin(), node.keys.end(), key);
    current = node.children[static_cast<std::size_t>(
        std::distance(node.keys.begin(), child))];
  }
}

BPlusTreeResult BPlusTree::erase(std::int64_t key) {
  if (!root_page_id_.has_value()) {
    return std::monostate{};
  }
  PageId current = *root_page_id_;
  while (true) {
    auto loaded = read_node(current);
    if (const auto* tree_error = std::get_if<BPlusTreeError>(&loaded)) {
      return *tree_error;
    }
    Node node = std::get<Node>(std::move(loaded));
    if (node.leaf) {
      const auto found = std::lower_bound(node.keys.begin(), node.keys.end(), key);
      if (found == node.keys.end() || *found != key) {
        return std::monostate{};
      }
      const std::size_t index = static_cast<std::size_t>(
          std::distance(node.keys.begin(), found));
      node.keys.erase(found);
      node.values.erase(node.values.begin() + static_cast<std::ptrdiff_t>(index));
      return write_node(node);
    }
    const auto child = std::upper_bound(node.keys.begin(), node.keys.end(), key);
    current = node.children[static_cast<std::size_t>(
        std::distance(node.keys.begin(), child))];
  }
}

BPlusTreeRangeResult BPlusTree::range(std::optional<std::int64_t> lower,
                                     std::optional<std::int64_t> upper) {
  std::vector<RowId> result;
  if (lower.has_value() && upper.has_value() && *lower > *upper) {
    return result;
  }
  if (!root_page_id_.has_value()) {
    return result;
  }
  PageId current = *root_page_id_;
  while (true) {
    auto loaded = read_node(current);
    if (const auto* tree_error = std::get_if<BPlusTreeError>(&loaded)) {
      return *tree_error;
    }
    const Node& node = std::get<Node>(loaded);
    if (node.leaf) {
      break;
    }
    const auto child = lower.has_value()
                           ? std::upper_bound(node.keys.begin(), node.keys.end(),
                                              *lower)
                           : node.keys.begin();
    current = node.children[static_cast<std::size_t>(
        std::distance(node.keys.begin(), child))];
  }
  while (true) {
    auto loaded = read_node(current);
    if (const auto* tree_error = std::get_if<BPlusTreeError>(&loaded)) {
      return *tree_error;
    }
    const Node& leaf = std::get<Node>(loaded);
    for (std::size_t index = 0; index < leaf.keys.size(); ++index) {
      if (lower.has_value() && leaf.keys[index] < *lower) {
        continue;
      }
      if (upper.has_value() && leaf.keys[index] > *upper) {
        return result;
      }
      result.push_back(leaf.values[index]);
    }
    if (!leaf.next_leaf.has_value()) {
      return result;
    }
    current = *leaf.next_leaf;
  }
}

DiskResult BPlusTree::flush() { return buffer_pool_.flush_all(); }

std::variant<BPlusTree::Node, BPlusTreeError> BPlusTree::read_node(
    PageId page_id) {
  auto fetched = buffer_pool_.fetch_page(page_id);
  if (const auto* pool_error = std::get_if<BufferPoolError>(&fetched)) {
    return buffer_error(*pool_error);
  }
  const auto guard = std::get<PageGuard>(std::move(fetched));
  const auto bytes = guard.bytes();
  if (read_u32(bytes, 0) != kMagic || read_u16(bytes, 4) != kVersion) {
    return error(BPlusTreeErrorCode::CorruptNode,
                 "invalid B+ tree node header");
  }
  const bool leaf = std::to_integer<std::uint8_t>(bytes[6]) == 1;
  const std::size_t count = read_u16(bytes, 8);
  const std::size_t entry_size = leaf ? kLeafEntrySize : kInternalEntrySize;
  if (count > max_keys_ || kHeaderSize + count * entry_size > kPageSize) {
    return error(BPlusTreeErrorCode::CorruptNode,
                 "invalid B+ tree node key count");
  }
  Node node{.page_id = page_id, .leaf = leaf};
  const std::uint32_t link = read_u32(bytes, 12);
  if (leaf) {
    if (link != kNoPage) {
      node.next_leaf = PageId{link};
    }
    for (std::size_t index = 0; index < count; ++index) {
      const std::size_t offset = kHeaderSize + index * kLeafEntrySize;
      node.keys.push_back(
          std::bit_cast<std::int64_t>(read_u64(bytes, offset)));
      node.values.push_back(RowId{PageId{read_u32(bytes, offset + 8)},
                                  SlotId{read_u16(bytes, offset + 12)}});
    }
  } else {
    node.children.push_back(PageId{link});
    for (std::size_t index = 0; index < count; ++index) {
      const std::size_t offset = kHeaderSize + index * kInternalEntrySize;
      node.keys.push_back(
          std::bit_cast<std::int64_t>(read_u64(bytes, offset)));
      node.children.push_back(PageId{read_u32(bytes, offset + 8)});
    }
  }
  if (!std::is_sorted(node.keys.begin(), node.keys.end()) ||
      (!leaf && node.children.size() != node.keys.size() + 1)) {
    return error(BPlusTreeErrorCode::CorruptNode,
                 "invalid B+ tree node contents");
  }
  return node;
}

BPlusTreeResult BPlusTree::write_node(const Node& node) {
  auto fetched = buffer_pool_.fetch_page(node.page_id);
  if (const auto* pool_error = std::get_if<BufferPoolError>(&fetched)) {
    return buffer_error(*pool_error);
  }
  auto guard = std::get<PageGuard>(std::move(fetched));
  auto bytes = guard.bytes();
  std::fill(bytes.begin(), bytes.end(), std::byte{0});
  write_u32(bytes, 0, kMagic);
  write_u16(bytes, 4, kVersion);
  bytes[6] = static_cast<std::byte>(node.leaf ? 1 : 0);
  write_u16(bytes, 8, static_cast<std::uint16_t>(node.keys.size()));
  write_u32(bytes, 12,
            node.leaf ? node.next_leaf.value_or(PageId{kNoPage}).value
                      : node.children.front().value);
  for (std::size_t index = 0; index < node.keys.size(); ++index) {
    const std::size_t offset =
        kHeaderSize + index * (node.leaf ? kLeafEntrySize : kInternalEntrySize);
    write_u64(bytes, offset,
              std::bit_cast<std::uint64_t>(node.keys[index]));
    if (node.leaf) {
      write_u32(bytes, offset + 8, node.values[index].page_id.value);
      write_u16(bytes, offset + 12, node.values[index].slot_id.value);
    } else {
      write_u32(bytes, offset + 8, node.children[index + 1].value);
    }
  }
  guard.mark_dirty();
  return std::monostate{};
}

std::variant<std::optional<BPlusTree::Split>, BPlusTreeError>
BPlusTree::insert_recursive(PageId page_id, std::int64_t key, RowId row_id) {
  auto loaded = read_node(page_id);
  if (const auto* tree_error = std::get_if<BPlusTreeError>(&loaded)) {
    return *tree_error;
  }
  Node node = std::get<Node>(std::move(loaded));
  if (node.leaf) {
    const auto position = std::lower_bound(node.keys.begin(), node.keys.end(), key);
    if (position != node.keys.end() && *position == key) {
      return error(BPlusTreeErrorCode::DuplicateKey,
                   "B+ tree key already exists");
    }
    const std::size_t index = static_cast<std::size_t>(
        std::distance(node.keys.begin(), position));
    node.keys.insert(position, key);
    node.values.insert(node.values.begin() + static_cast<std::ptrdiff_t>(index),
                       row_id);
    if (node.keys.size() <= max_keys_) {
      const auto written = write_node(node);
      if (const auto* tree_error = std::get_if<BPlusTreeError>(&written)) {
        return *tree_error;
      }
      return std::optional<Split>{};
    }
    auto allocated = buffer_pool_.allocate_page();
    if (const auto* pool_error = std::get_if<BufferPoolError>(&allocated)) {
      return buffer_error(*pool_error);
    }
    auto guard = std::get<PageGuard>(std::move(allocated));
    const std::size_t middle = node.keys.size() / 2;
    Node right{.page_id = guard.page_id(),
               .leaf = true,
               .keys = {node.keys.begin() + static_cast<std::ptrdiff_t>(middle),
                        node.keys.end()},
               .values = {node.values.begin() + static_cast<std::ptrdiff_t>(middle),
                          node.values.end()},
               .children = {},
               .next_leaf = node.next_leaf};
    guard = PageGuard{};
    node.keys.erase(node.keys.begin() + static_cast<std::ptrdiff_t>(middle),
                    node.keys.end());
    node.values.erase(node.values.begin() + static_cast<std::ptrdiff_t>(middle),
                      node.values.end());
    node.next_leaf = right.page_id;
    auto written = write_node(node);
    if (const auto* tree_error = std::get_if<BPlusTreeError>(&written)) {
      return *tree_error;
    }
    written = write_node(right);
    if (const auto* tree_error = std::get_if<BPlusTreeError>(&written)) {
      return *tree_error;
    }
    return std::optional<Split>{Split{right.keys.front(), right.page_id}};
  }

  const auto child_position = std::upper_bound(node.keys.begin(), node.keys.end(), key);
  const std::size_t child_index = static_cast<std::size_t>(
      std::distance(node.keys.begin(), child_position));
  auto child_result = insert_recursive(node.children[child_index], key, row_id);
  if (const auto* tree_error = std::get_if<BPlusTreeError>(&child_result)) {
    return *tree_error;
  }
  const auto& child_split = std::get<std::optional<Split>>(child_result);
  if (!child_split.has_value()) {
    return std::optional<Split>{};
  }
  node.keys.insert(node.keys.begin() + static_cast<std::ptrdiff_t>(child_index),
                   child_split->separator);
  node.children.insert(
      node.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1),
      child_split->right_page_id);
  if (node.keys.size() <= max_keys_) {
    const auto written = write_node(node);
    if (const auto* tree_error = std::get_if<BPlusTreeError>(&written)) {
      return *tree_error;
    }
    return std::optional<Split>{};
  }
  auto allocated = buffer_pool_.allocate_page();
  if (const auto* pool_error = std::get_if<BufferPoolError>(&allocated)) {
    return buffer_error(*pool_error);
  }
  auto guard = std::get<PageGuard>(std::move(allocated));
  const std::size_t middle = node.keys.size() / 2;
  const std::int64_t separator = node.keys[middle];
  Node right{.page_id = guard.page_id(),
             .leaf = false,
             .keys = {node.keys.begin() + static_cast<std::ptrdiff_t>(middle + 1),
                      node.keys.end()},
             .values = {},
             .children = {node.children.begin() +
                              static_cast<std::ptrdiff_t>(middle + 1),
                          node.children.end()}};
  guard = PageGuard{};
  node.keys.erase(node.keys.begin() + static_cast<std::ptrdiff_t>(middle),
                  node.keys.end());
  node.children.erase(
      node.children.begin() + static_cast<std::ptrdiff_t>(middle + 1),
      node.children.end());
  auto written = write_node(node);
  if (const auto* tree_error = std::get_if<BPlusTreeError>(&written)) {
    return *tree_error;
  }
  written = write_node(right);
  if (const auto* tree_error = std::get_if<BPlusTreeError>(&written)) {
    return *tree_error;
  }
  return std::optional<Split>{Split{separator, right.page_id}};
}

}  // namespace curiodb::storage
