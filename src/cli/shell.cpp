#include "curiodb/cli/shell.hpp"

#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

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

}  // namespace

Shell::Shell(std::istream& input, std::ostream& output)
    : input_(input), output_(output) {}

int Shell::run() {
  output_ << kName << " v" << kVersion << '\n';

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

  execution::StatementExecutor executor{catalog_};
  const auto result =
      executor.execute(std::get<sql::Statement>(parse_result));
  if (!result.success) {
    output_ << "Error: ";
  }
  output_ << result.message << '\n';
}

void Shell::execute_meta_command(const std::string& command) {
  if (command == ".help") {
    output_ << ".databases       List databases\n"
               ".tables          List tables in the active database\n"
               ".schema <table>  Show a table schema\n"
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
