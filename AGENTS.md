# Atlas Agent Context & Guidelines

## Project Overview
- **Project**: Atlas — RISC-V computer architecture teaching platform forked from an upstream simulator at v1.1.8 (commit `7ddb6f3`); see `NOTICE.md`.
- **Tech Stack**: C++20, Qt6 (Qt 6.11), CMake, Ninja.
- **Platform**: Fedora Linux (Workstation Edition).

## Standard Build Commands
- **Application Build**:
  ```bash
  cmake -S . -B build -G Ninja && cmake --build build
  # Or use the helper script:
  ./scripts/build.sh
  ```
  Binaries generated: `build/Atlas` (GUI) and `build/atlas-cli` (CLI).

- **Test Build and Execution**:
  ```bash
  cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
  # Or use the helper script (prints: total / passed / failed):
  ./scripts/test.sh
  ```
  Test binary: `build-tests/test/atlas_test`.
  > **Note**: Run tests via `ctest --test-dir build-tests` (or `./scripts/test.sh`). Do not run `atlas_test` directly from the repo root because tests load files via relative path `"../examples/..."` (~100 occurrences); CTest properly executes them with working directory `test/` configured by `gtest_discover_tests`.

## Development Rules & Constraints
1. **One feature per branch**: Keep logical changes isolated on designated topic branches.
2. **One commit per working change**: Atomic, well-explained commits.
3. **Continuous verification**: Build and run tests via `ctest --test-dir build-tests` after every change.
4. **Core logic isolation**: Do NOT modify simulator core logic (RVSS/RV5S models, cache hierarchy, assembler semantics) during UI tasks unless explicitly requested.
5. **Clean compiler output**: New code must be warning-free under `-Wall -Wextra`.
6. **Naming**: The project was renamed to Atlas in the `final/atlas` rebrand. Use `Atlas` for namespaces, identifiers, targets and user-facing text; the upstream name may appear only in `LICENSE.md`, `NOTICE.md`, the changes document that `NOTICE.md` links to, and `docs/audit/**`. The changes document's filename (as a filename only, never as a name in prose) may also appear in `scripts/make_submission.sh`, `scripts/verify_submission.sh`, the `README_SUBMISSION.md` they generate, and `docs/README.md`.
7. **Dependency discipline**: Always ask the user before introducing any new external dependencies or libraries.
8. **Safety & Non-destructive operations**: Never execute destructive git commands (`git push`, `git reset --hard`, `git clean`) or delete files outside build directories without explicit user confirmation.

## Known Baseline Issues & Verified Facts
1. **Test Suite Status & Harness Resolution**:
   - Total registered tests: **4,326** (final/metrics; 158 at the original audit).
   - Test execution result: **4,326/4,326 pass** under serial `ctest --test-dir build-tests`. Do not use `ctest -j`: the GUI tests share `test/vm_state/temp.asm` and flake in parallel (known_issues 150, `docs/TESTING.md`).
   - *Harness Root Cause 1*: `test/test_main.cpp` originally lacked a `QApplication`. Constructing any RVSS/RV5S processor builds a `QGraphicsScene`, which crashed with `SIGSEGV` when uninitialized in headless environments. Fixed with an offscreen headless `QApplication` in `test_main.cpp`.
   - *Harness Root Cause 2*: Tests open `"../examples/..."` (~100 occurrences), requiring execution with working directory set to `test/`. CTest sets this automatically via `gtest_discover_tests` `WORKING_DIRECTORY`.
   - Earlier "43 pass / 115 fail" figures were due to running without QApplication / wrong working directory, not simulator logic bugs.
2. **Windows Header Portability**: `test/test_main.cpp` originally included `<windows.h>` unconditionally. Guarded with `#ifdef _WIN32 ... #endif` on Linux.
3. **`getDiagnostics` removed (E8)**: its body was commented out, with no return statement (`-Wreturn-type` plus two `-Wunused-parameter`), and it had zero callers. It was deleted; the assembler corpus is byte-identical.
4. **Build Warnings**: 23 compiler and linker warning lines under GCC 16 on Fedora 44 (detailed in `docs/audit/baseline_warnings.txt`).
5. **Disabled Tests (5 remaining, of 18; E8)**:
   - The 8 commented-out pipeline `TEST`s and the 4 `#if 0` replacement-policy tests were re-enabled in E8 (see `docs/audit/disabled_tests.md`).
   - **5 `#if 0` blocks remain**: 2 in `test/pseudo_inst_test.cpp` (do not compile), 3 in `test/rv5s_f_extension_test.cpp` (FP hazard cases).
