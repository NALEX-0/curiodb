#include "curiodb/catalog/catalog_storage.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "curiodb/storage/disk_manager.hpp"
#include "curiodb/storage/slotted_page.hpp"
#include "curiodb/types/data_type.hpp"

namespace curiodb::catalog {
namespace {

constexpr std::array<char, 8> kMagic{'C', 'U', 'R', 'I', 'O', 'D', 'B', 'C'};
constexpr std::uint32_t kFormatVersion = 3;
constexpr std::size_t kHeaderSize = kMagic.size() + sizeof(std::uint32_t) * 2;

CatalogStorageError error(CatalogStorageErrorCode code, std::string message) {
  return {code, std::move(message)};
}

CatalogStorageError disk_error(const storage::DiskError& disk_error) {
  return error(CatalogStorageErrorCode::DiskError, disk_error.message);
}

template <typename Integer>
void append_integer(std::vector<std::byte>& output, Integer value) {
  static_assert(std::is_unsigned_v<Integer>);
  std::uint64_t remaining = value;
  for (std::size_t index = 0; index < sizeof(Integer); ++index) {
    output.push_back(static_cast<std::byte>(remaining & 0xffU));
    remaining >>= 8U;
  }
}

bool append_string(std::vector<std::byte>& output, const std::string& value) {
  if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  append_integer(output, static_cast<std::uint32_t>(value.size()));
  for (const char character : value) {
    output.push_back(static_cast<std::byte>(character));
  }
  return true;
}

std::variant<std::vector<std::byte>, CatalogStorageError> serialize(
    const StoredCatalog& catalog) {
  if (catalog.tables.size() > std::numeric_limits<std::uint32_t>::max()) {
    return error(CatalogStorageErrorCode::MetadataTooLarge,
                 "catalog contains too many tables");
  }

  std::vector<std::byte> output;
  if (!append_string(output, catalog.database_name)) {
    return error(CatalogStorageErrorCode::MetadataTooLarge,
                 "database name is too large");
  }
  append_integer(output, static_cast<std::uint32_t>(catalog.tables.size()));
  for (const auto& table : catalog.tables) {
    if (!append_string(output, table.schema.name) ||
        table.schema.columns.size() >
            std::numeric_limits<std::uint32_t>::max() ||
        table.page_ids.size() > std::numeric_limits<std::uint32_t>::max()) {
      return error(CatalogStorageErrorCode::MetadataTooLarge,
                   "table metadata is too large");
    }
    append_integer(
        output, static_cast<std::uint32_t>(table.schema.columns.size()));
    for (const auto& column : table.schema.columns) {
      if (!append_string(output, column.name)) {
        return error(CatalogStorageErrorCode::MetadataTooLarge,
                     "column name is too large");
      }
      append_integer(output, static_cast<std::uint8_t>(column.type.kind));
      const auto length = column.type.length.value_or(0);
      if (length > std::numeric_limits<std::uint32_t>::max()) {
        return error(CatalogStorageErrorCode::MetadataTooLarge,
                     "column length is too large");
      }
      append_integer(output, static_cast<std::uint32_t>(length));
      std::uint8_t constraints = 0;
      constraints |= column.primary_key ? 1U : 0U;
      constraints |= column.unique ? 2U : 0U;
      constraints |= column.not_null ? 4U : 0U;
      append_integer(output, constraints);
    }
    append_integer(output,
                   static_cast<std::uint32_t>(table.page_ids.size()));
    for (const storage::PageId page_id : table.page_ids) {
      append_integer(output, page_id.value);
    }
    if (table.indexes.size() > std::numeric_limits<std::uint32_t>::max()) {
      return error(CatalogStorageErrorCode::MetadataTooLarge,
                   "table contains too many indexes");
    }
    append_integer(output, static_cast<std::uint32_t>(table.indexes.size()));
    for (const auto& index : table.indexes) {
      if (!append_string(output, index.name) ||
          !append_string(output, index.column_name)) {
        return error(CatalogStorageErrorCode::MetadataTooLarge,
                     "index name is too large");
      }
      append_integer(output,
                     index.root_page_id.value_or(
                         storage::PageId{std::numeric_limits<std::uint32_t>::max()})
                         .value);
    }
  }
  if (output.size() > storage::kPageSize - kHeaderSize) {
    return error(CatalogStorageErrorCode::MetadataTooLarge,
                 "catalog metadata does not fit in its page");
  }
  return output;
}

class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  template <typename Integer>
  bool read_integer(Integer& value) {
    static_assert(std::is_unsigned_v<Integer>);
    if (remaining() < sizeof(Integer)) {
      return false;
    }
    value = 0;
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
      value |= static_cast<Integer>(std::to_integer<unsigned int>(
                   bytes_[position_ + index]))
               << (index * 8U);
    }
    position_ += sizeof(Integer);
    return true;
  }

  bool read_string(std::string& value) {
    std::uint32_t size = 0;
    if (!read_integer(size) || remaining() < size) {
      return false;
    }
    value.clear();
    value.reserve(size);
    for (std::uint32_t index = 0; index < size; ++index) {
      value.push_back(static_cast<char>(
          std::to_integer<unsigned char>(bytes_[position_ + index])));
    }
    position_ += size;
    return true;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return bytes_.size() - position_;
  }

 private:
  std::span<const std::byte> bytes_;
  std::size_t position_{0};
};

CatalogLoadResult deserialize(std::span<const std::byte> bytes,
                              std::uint32_t version) {
  Reader reader{bytes};
  StoredCatalog catalog;
  std::uint32_t table_count = 0;
  if (!reader.read_string(catalog.database_name) ||
      !reader.read_integer(table_count)) {
    return error(CatalogStorageErrorCode::CorruptMetadata,
                 "catalog metadata is truncated");
  }
  catalog.tables.reserve(table_count);
  for (std::uint32_t table_index = 0; table_index < table_count;
       ++table_index) {
    StoredTable table;
    std::uint32_t column_count = 0;
    if (!reader.read_string(table.schema.name) ||
        !reader.read_integer(column_count)) {
      return error(CatalogStorageErrorCode::CorruptMetadata,
                   "table metadata is truncated");
    }
    table.schema.columns.reserve(column_count);
    for (std::uint32_t column_index = 0; column_index < column_count;
         ++column_index) {
      ColumnSchema column;
      std::uint8_t kind = 0;
      std::uint32_t length = 0;
      if (!reader.read_string(column.name) || !reader.read_integer(kind) ||
          !reader.read_integer(length) ||
          kind > static_cast<std::uint8_t>(DataTypeKind::Varchar)) {
        return error(CatalogStorageErrorCode::CorruptMetadata,
                     "column metadata is invalid");
      }
      column.type.kind = static_cast<DataTypeKind>(kind);
      if (column.type.kind == DataTypeKind::Varchar) {
        if (length == 0) {
          return error(CatalogStorageErrorCode::CorruptMetadata,
                       "VARCHAR length must be positive");
        }
        column.type.length = length;
      } else if (length != 0) {
        return error(CatalogStorageErrorCode::CorruptMetadata,
                     "fixed-size type has an unexpected length");
      }
      if (version >= 3) {
        std::uint8_t constraints = 0;
        if (!reader.read_integer(constraints) || constraints > 7U) {
          return error(CatalogStorageErrorCode::CorruptMetadata,
                       "column constraints are invalid");
        }
        column.primary_key = (constraints & 1U) != 0;
        column.unique = (constraints & 2U) != 0;
        column.not_null = (constraints & 4U) != 0;
      }
      table.schema.columns.push_back(std::move(column));
    }
    std::uint32_t page_count = 0;
    if (!reader.read_integer(page_count)) {
      return error(CatalogStorageErrorCode::CorruptMetadata,
                   "table page list is truncated");
    }
    table.page_ids.reserve(page_count);
    for (std::uint32_t page_index = 0; page_index < page_count; ++page_index) {
      std::uint32_t page_id = 0;
      if (!reader.read_integer(page_id) || page_id == kCatalogPageId.value) {
        return error(CatalogStorageErrorCode::CorruptMetadata,
                     "table page identifier is invalid");
      }
      table.page_ids.push_back(storage::PageId{page_id});
    }
    if (version >= 2) {
      std::uint32_t index_count = 0;
      if (!reader.read_integer(index_count)) {
        return error(CatalogStorageErrorCode::CorruptMetadata,
                     "table index list is truncated");
      }
      table.indexes.reserve(index_count);
      for (std::uint32_t index_number = 0; index_number < index_count;
           ++index_number) {
        StoredIndex index;
        std::uint32_t root_page_id = 0;
        if (!reader.read_string(index.name) ||
            !reader.read_string(index.column_name) ||
            !reader.read_integer(root_page_id) || root_page_id == 0) {
          return error(CatalogStorageErrorCode::CorruptMetadata,
                       "index metadata is invalid");
        }
        if (root_page_id != std::numeric_limits<std::uint32_t>::max()) {
          index.root_page_id = storage::PageId{root_page_id};
        }
        table.indexes.push_back(std::move(index));
      }
    }
    catalog.tables.push_back(std::move(table));
  }
  if (reader.remaining() != 0) {
    return error(CatalogStorageErrorCode::CorruptMetadata,
                 "catalog metadata contains trailing bytes");
  }
  return catalog;
}

}  // namespace

CatalogStorageResult initialize_catalog_storage(
    storage::DiskManager& disk_manager, const StoredCatalog& catalog) {
  if (disk_manager.page_count() != 0) {
    return error(CatalogStorageErrorCode::DatabaseNotEmpty,
                 "catalog can only be initialized in an empty database");
  }
  const auto serialized = serialize(catalog);
  if (std::holds_alternative<CatalogStorageError>(serialized)) {
    return std::get<CatalogStorageError>(serialized);
  }
  const auto allocation = disk_manager.allocate_page();
  if (std::holds_alternative<storage::DiskError>(allocation)) {
    return disk_error(std::get<storage::DiskError>(allocation));
  }
  if (std::get<storage::PageId>(allocation) != kCatalogPageId) {
    return error(CatalogStorageErrorCode::DiskError,
                 "catalog page was not allocated at page zero");
  }
  return store_catalog(disk_manager, catalog);
}

CatalogStorageResult store_catalog(storage::DiskManager& disk_manager,
                                   const StoredCatalog& catalog) {
  if (disk_manager.page_count() == 0) {
    return error(CatalogStorageErrorCode::CorruptMetadata,
                 "database does not contain a catalog page");
  }
  auto serialized = serialize(catalog);
  if (std::holds_alternative<CatalogStorageError>(serialized)) {
    return std::get<CatalogStorageError>(std::move(serialized));
  }

  storage::PageBytes page{};
  std::memcpy(page.data(), kMagic.data(), kMagic.size());
  std::size_t position = kMagic.size();
  const auto write_header_integer = [&page, &position](std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
      page[position++] = static_cast<std::byte>(value & 0xffU);
      value >>= 8U;
    }
  };
  write_header_integer(kFormatVersion);
  const auto& payload = std::get<std::vector<std::byte>>(serialized);
  write_header_integer(static_cast<std::uint32_t>(payload.size()));
  std::copy(payload.begin(), payload.end(), page.begin() + position);

  const auto write = disk_manager.write_page(kCatalogPageId, page);
  if (std::holds_alternative<storage::DiskError>(write)) {
    return disk_error(std::get<storage::DiskError>(write));
  }
  const auto flush = disk_manager.flush();
  if (std::holds_alternative<storage::DiskError>(flush)) {
    return disk_error(std::get<storage::DiskError>(flush));
  }
  return std::monostate{};
}

CatalogLoadResult load_catalog(storage::DiskManager& disk_manager) {
  const auto read = disk_manager.read_page(kCatalogPageId);
  if (std::holds_alternative<storage::DiskError>(read)) {
    return disk_error(std::get<storage::DiskError>(read));
  }
  const auto& page = std::get<storage::PageBytes>(read);
  if (!std::equal(kMagic.begin(), kMagic.end(),
                  reinterpret_cast<const char*>(page.data()))) {
    return error(CatalogStorageErrorCode::InvalidMagic,
                 "page zero is not a CurioDB catalog page");
  }
  Reader header{std::span<const std::byte>{page}.subspan(kMagic.size())};
  std::uint32_t version = 0;
  std::uint32_t payload_size = 0;
  if (!header.read_integer(version) || !header.read_integer(payload_size)) {
    return error(CatalogStorageErrorCode::CorruptMetadata,
                 "catalog header is truncated");
  }
  if (version < 1 || version > kFormatVersion) {
    return error(CatalogStorageErrorCode::UnsupportedVersion,
                 "catalog format version is not supported");
  }
  if (payload_size > storage::kPageSize - kHeaderSize) {
    return error(CatalogStorageErrorCode::CorruptMetadata,
                 "catalog payload size is invalid");
  }
  return deserialize(std::span<const std::byte>{page}.subspan(
                         kHeaderSize, payload_size),
                     version);
}

}  // namespace curiodb::catalog
