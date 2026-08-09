# CurioDB

CurioDB is a small relational database built from first principles in modern C++.
Milestone 1 provides the project foundation and an interactive command-line shell;
SQL parsing and storage are intentionally left for later milestones.

## Requirements

- CMake 3.20 or newer
- A C++20 compiler (Apple Clang is the primary macOS target)
- Git, if you want version control

GoogleTest 1.15.2 is downloaded by CMake only when tests are enabled.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

To build without downloading GoogleTest:

```sh
cmake -S . -B build -DCURIODB_BUILD_TESTS=OFF
cmake --build build
```

Run the shell with `./build/curiodb`. Use `.help` for available commands and
`.quit` to exit.
