#include <filesystem>
#include <iostream>
#include <string_view>

#include "curiodb/cli/shell.hpp"
#include "curiodb/version.hpp"

namespace {

void print_usage(std::ostream& output) {
  output << "Usage: curiodb [--help] [--version]\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc > 2) {
    print_usage(std::cerr);
    return 2;
  }

  if (argc == 2) {
    const std::string_view argument{argv[1]};
    if (argument == "--help" || argument == "-h") {
      print_usage(std::cout);
      return 0;
    }
    if (argument == "--version") {
      std::cout << curiodb::kName << ' ' << curiodb::kVersion << '\n';
      return 0;
    }

    std::cerr << "Unknown option: " << argument << '\n';
    print_usage(std::cerr);
    return 2;
  }

  curiodb::cli::Shell shell{std::cin, std::cout,
                            std::filesystem::current_path() / ".curiodb"};
  return shell.run();
}
