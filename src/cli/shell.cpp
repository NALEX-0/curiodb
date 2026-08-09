#include "curiodb/cli/shell.hpp"

#include <istream>
#include <ostream>
#include <string>

#include "curiodb/version.hpp"

namespace curiodb::cli {

Shell::Shell(std::istream& input, std::ostream& output)
    : input_(input), output_(output) {}

int Shell::run() {
  output_ << kName << " v" << kVersion << '\n';

  std::string line;
  while (output_ << "curiodb> " && std::getline(input_, line)) {
    if (line == ".quit" || line == ".exit") {
      break;
    }
    if (line == ".help") {
      output_ << ".help  Show this help\n"
                 ".quit  Exit CurioDB\n";
      continue;
    }
    if (!line.empty()) {
      output_ << "SQL execution is not available yet.\n";
    }
  }

  return 0;
}

}  // namespace curiodb::cli
