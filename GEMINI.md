# Atlas Agent Context & Guidelines (Gemini / Antigravity)

See [AGENTS.md](AGENTS.md) for primary instructions.

## Quick Summary
- **Project**: Atlas (RISC-V architecture platform based on Kites v1.1.8 / commit `7ddb6f3`)
- **Environment**: Fedora Linux, C++20, Qt6, CMake, Ninja, GCC 16
- **Build**: `cmake -S . -B build -G Ninja && cmake --build build`
- **Tests**: `cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure`
  - Always run tests via `ctest --test-dir build-tests` (tests open `../examples/...` and require working directory `test/`).
  - Active test baseline: **158/158 pass** under CTest.
- **Core Rules**:
  - One feature per branch; one commit per logical working change.
  - Test after every change (`ctest --test-dir build-tests`).
  - Do not modify core simulator logic during UI tasks.
  - Zero warnings on new code under `-Wall -Wextra`.
  - Keep `Kites` names until a dedicated rename commit.
  - No destructive actions (`git push`, `git reset --hard`, `git clean`, deleting outside build directories) without asking.
