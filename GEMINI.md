# Atlas Agent Context & Guidelines (Gemini / Antigravity)

See [AGENTS.md](AGENTS.md) for primary instructions.

## Quick Summary
- **Project**: Atlas (RISC-V architecture platform forked from an upstream simulator at v1.1.8 / commit `7ddb6f3`; see NOTICE.md)
- **Environment**: Fedora Linux, C++20, Qt6, CMake, Ninja, GCC 16
- **Build**: `cmake -S . -B build -G Ninja && cmake --build build` (or `./scripts/build.sh`)
- **Tests**: `cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure` (or `./scripts/test.sh`)
  - Always run tests via `ctest --test-dir build-tests` (tests open `../examples/...` and require working directory `test/`).
  - Active test baseline: **4,363/4,363 pass** under serial CTest.
- **Core Rules**:
  - One feature per branch; one commit per logical working change.
  - Test after every change (`ctest --test-dir build-tests`).
  - Do not modify core simulator logic during UI tasks.
  - Zero warnings on new code under `-Wall -Wextra`.
  - Use `Atlas` names; the upstream name stays only in LICENSE.md, NOTICE.md, the changes document NOTICE.md links to, and docs/audit/.
  - No destructive actions (`git push`, `git reset --hard`, `git clean`, deleting outside build directories) without asking.
