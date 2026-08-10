#pragma once

#include <iosfwd>
#include <string>

#include "curiodb/catalog/catalog.hpp"
#include "curiodb/storage/in_memory_storage.hpp"

namespace curiodb::cli {

class Shell {
 public:
  Shell(std::istream& input, std::ostream& output);

  [[nodiscard]] int run();

 private:
  void execute_sql(const std::string& sql);
  void execute_meta_command(const std::string& command);

  std::istream& input_;
  std::ostream& output_;
  catalog::Catalog catalog_;
  storage::InMemoryStorage storage_;
};

}  // namespace curiodb::cli
