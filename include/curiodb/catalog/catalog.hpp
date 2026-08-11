#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "curiodb/types/data_type.hpp"

namespace curiodb::catalog {

struct ColumnSchema {
  std::string name;
  DataType type;

  [[nodiscard]] friend bool operator==(
      const ColumnSchema&, const ColumnSchema&) = default;
};

struct TableSchema {
  std::string name;
  std::vector<ColumnSchema> columns;

  [[nodiscard]] friend bool operator==(
      const TableSchema&, const TableSchema&) = default;
};

enum class CatalogErrorCode {
  InvalidName,
  DatabaseAlreadyExists,
  DatabaseNotFound,
  NoDatabaseSelected,
  TableAlreadyExists,
  EmptyTable,
  DuplicateColumn,
};

struct CatalogError {
  CatalogErrorCode code;
  std::string message;

  [[nodiscard]] friend bool operator==(
      const CatalogError&, const CatalogError&) = default;
};

using CatalogResult = std::optional<CatalogError>;

class Catalog {
 public:
  [[nodiscard]] CatalogResult create_database(std::string name);
  [[nodiscard]] CatalogResult use_database(std::string_view name);
  void clear_selection() noexcept;
  [[nodiscard]] CatalogResult create_table(
      std::string name, std::vector<ColumnSchema> columns);

  [[nodiscard]] std::optional<std::string> active_database() const;
  [[nodiscard]] std::vector<std::string> database_names() const;
  [[nodiscard]] std::vector<std::string> table_names() const;
  [[nodiscard]] const TableSchema* find_table(std::string_view name) const;

 private:
  struct Database {
    std::string name;
    std::unordered_map<std::string, TableSchema> tables;
  };

  [[nodiscard]] const Database* selected_database() const;
  [[nodiscard]] Database* selected_database();

  std::unordered_map<std::string, Database> databases_;
  std::optional<std::string> active_database_key_;
};

}  // namespace curiodb::catalog
