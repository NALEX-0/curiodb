#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "curiodb/cli/shell.hpp"

TEST(ShellTest, DisplaysPromptAndQuitsCleanly) {
  std::istringstream input{".quit\n"};
  std::ostringstream output;
  curiodb::cli::Shell shell{input, output};

  EXPECT_EQ(shell.run(), 0);
  EXPECT_NE(output.str().find("CurioDB v0.1.0"), std::string::npos);
  EXPECT_NE(output.str().find("curiodb> "), std::string::npos);
}

TEST(ShellTest, ShowsHelp) {
  std::istringstream input{".help\n.quit\n"};
  std::ostringstream output;
  curiodb::cli::Shell shell{input, output};

  EXPECT_EQ(shell.run(), 0);
  EXPECT_NE(output.str().find(".help  Show this help"), std::string::npos);
}
