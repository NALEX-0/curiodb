#pragma once

#include <iosfwd>

namespace curiodb::cli {

class Shell {
 public:
  Shell(std::istream& input, std::ostream& output);

  [[nodiscard]] int run();

 private:
  std::istream& input_;
  std::ostream& output_;
};

}  // namespace curiodb::cli
