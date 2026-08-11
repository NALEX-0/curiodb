#pragma once

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/storage/in_memory_storage.hpp"
#include "curiodb/storage/disk_storage.hpp"

namespace curiodb::cli {

class Shell {
 public:
  Shell(std::istream& input, std::ostream& output);
  Shell(std::istream& input, std::ostream& output,
        std::filesystem::path data_directory);

  [[nodiscard]] int run();

 private:
  void execute_sql(const std::string& sql);
  void execute_meta_command(const std::string& command);

  std::istream& input_;
  std::ostream& output_;
  catalog::Catalog catalog_;
  storage::InMemoryStorage storage_;
  std::unique_ptr<storage::DiskStorage> disk_storage_;
  std::string startup_error_;
};

}  // namespace curiodb::cli
