#include "curiodb/cli/shell.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "curiodb/execution/statement_executor.hpp"
#include "curiodb/sql/lexer.hpp"
#include "curiodb/sql/parser.hpp"
#include "curiodb/types/data_type.hpp"
#include "curiodb/version.hpp"

namespace curiodb::cli {
namespace {

bool statement_complete(const std::string& sql) {
  const auto tokens = sql::Lexer{sql}.tokenize();
  return tokens.size() >= 2 &&
         tokens[tokens.size() - 2].type == sql::TokenType::Semicolon;
}

void print_query(std::ostream& output,
                 const execution::QueryResult& query) {
  std::vector<std::size_t> widths;
  widths.reserve(query.columns.size());
  for (const auto& column : query.columns) {
    widths.push_back(column.size());
  }
  for (const auto& row : query.rows) {
    for (std::size_t index = 0; index < row.size(); ++index) {
      widths[index] = std::max(widths[index], row[index].size());
    }
  }

  const auto print_values = [&](const std::vector<std::string>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (index != 0) {
        output << " | ";
      }
      output << std::left << std::setw(static_cast<int>(widths[index]))
             << values[index];
    }
    output << '\n';
  };

  print_values(query.columns);
  for (std::size_t index = 0; index < widths.size(); ++index) {
    if (index != 0) {
      output << "-+-";
    }
    output << std::string(widths[index], '-');
  }
  output << '\n';
  for (const auto& row : query.rows) {
    print_values(row);
  }
}

}  // namespace

Shell::Shell(std::istream& input, std::ostream& output)
    : input_(input), output_(output) {}

Shell::Shell(std::istream& input, std::ostream& output,
             std::filesystem::path data_directory)
    : input_(input),
      output_(output),
      disk_storage_(
          std::make_unique<storage::DiskStorage>(std::move(data_directory))) {
  const auto opened = disk_storage_->open();
  if (const auto* error = std::get_if<storage::DiskStorageError>(&opened)) {
    startup_error_ = error->message;
    return;
  }
  for (const auto& stored : disk_storage_->catalogs()) {
    if (auto result = catalog_.create_database(stored.database_name);
        result.has_value()) {
      startup_error_ = result->message;
      return;
    }
    if (auto result = catalog_.use_database(stored.database_name);
        result.has_value()) {
      startup_error_ = result->message;
      return;
    }
    for (const auto& table : stored.tables) {
      if (auto result = catalog_.create_table(table.schema.name,
                                              table.schema.columns);
          result.has_value()) {
        startup_error_ = result->message;
        return;
      }
    }
  }
  catalog_.clear_selection();
}

int Shell::run() {
  output_ << kName << " v" << kVersion << '\n';
  if (!startup_error_.empty()) {
    output_ << "Error opening data directory: " << startup_error_ << '\n';
    return 1;
  }

  std::string line;
  std::string pending_sql;
  while (output_ << (pending_sql.empty() ? "curiodb> " : "   ...> ") &&
         std::getline(input_, line)) {
    if (pending_sql.empty() && (line == ".quit" || line == ".exit")) {
      break;
    }
    if (pending_sql.empty() && !line.empty() && line.front() == '.') {
      execute_meta_command(line);
      continue;
    }
    if (line.empty() && pending_sql.empty()) {
      continue;
    }

    if (!pending_sql.empty()) {
      pending_sql.push_back('\n');
    }
    pending_sql += line;
    if (statement_complete(pending_sql)) {
      execute_sql(pending_sql);
      pending_sql.clear();
    }
  }

  if (!pending_sql.empty()) {
    execute_sql(pending_sql);
  }

  return 0;
}

void Shell::execute_sql(const std::string& sql_text) {
  auto parse_result =
      sql::Parser{sql::Lexer{sql_text}.tokenize()}.parse_statement();
  if (const auto* error = std::get_if<sql::ParseError>(&parse_result)) {
    output_ << "Error at line " << error->location.line << ", column "
            << error->location.column << ": " << error->message << '\n';
    return;
  }

  const auto& statement = std::get<sql::Statement>(parse_result);
  const auto result = disk_storage_ != nullptr
                          ? execution::StatementExecutor{catalog_,
                                                         *disk_storage_}
                                .execute(statement)
                          : execution::StatementExecutor{catalog_, storage_}
                                .execute(statement);
  if (!result.success) {
    output_ << "Error: ";
  }
  if (result.query.has_value()) {
    print_query(output_, *result.query);
  }
  output_ << result.message << '\n';
}

void Shell::execute_meta_command(const std::string& command) {
  if (command == ".help") {
    output_ << ".databases       List databases\n"
               ".tables          List tables in the active database\n"
               ".schema <table>  Show a table schema\n"
               "EXPLAIN SELECT   Show the selected query plan\n"
               ".help            Show this help\n"
               ".quit            Exit CurioDB\n";
    return;
  }
  if (command == ".databases") {
    const auto names = catalog_.database_names();
    if (names.empty()) {
      output_ << "No databases.\n";
    } else {
      for (const auto& name : names) {
        output_ << name << '\n';
      }
    }
    return;
  }
  if (command == ".tables") {
    if (!catalog_.active_database().has_value()) {
      output_ << "Error: no database selected; use USE <database> first\n";
      return;
    }
    const auto names = catalog_.table_names();
    if (names.empty()) {
      output_ << "No tables.\n";
    } else {
      for (const auto& name : names) {
        output_ << name << '\n';
      }
    }
    return;
  }

  constexpr std::string_view schema_prefix = ".schema";
  if (command == schema_prefix || command.starts_with(".schema ")) {
    std::istringstream arguments{command};
    std::string meta_command;
    std::string table_name;
    std::string extra;
    arguments >> meta_command >> table_name >> extra;
    if (table_name.empty() || !extra.empty()) {
      output_ << "Usage: .schema <table>\n";
      return;
    }
    if (!catalog_.active_database().has_value()) {
      output_ << "Error: no database selected; use USE <database> first\n";
      return;
    }
    const catalog::TableSchema* const table = catalog_.find_table(table_name);
    if (table == nullptr) {
      output_ << "Error: table '" << table_name << "' does not exist\n";
      return;
    }
    output_ << "CREATE TABLE " << table->name << " (\n";
    for (std::size_t index = 0; index < table->columns.size(); ++index) {
      const auto& column = table->columns[index];
      output_ << "  " << column.name << ' ' << format_data_type(column.type);
      output_ << (index + 1 < table->columns.size() ? ",\n" : "\n");
    }
    output_ << ");\n";
    return;
  }

  output_ << "Unknown command: " << command << '\n';
}

}  // namespace curiodb::cli
