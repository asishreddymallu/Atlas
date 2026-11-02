# Atlas Agent Context & Guidelines

## Project Overview
- **Project**: Atlas — RISC-V computer architecture teaching platform built on Kites simulator (upstream commit `7ddb6f3`, v1.1.8).
- **Tech Stack**: C++20, Qt6 (Qt 6.11), CMake, Ninja.
- **Platform**: Fedora Linux (Workstation Edition).

## Standard Build Commands
- **Application Build**:
  ```bash
  cmake -S . -B build -G Ninja && cmake --build build
  ```
  Binaries generated: `build/Kites` (GUI) and `build/kites-cli` (CLI).

- **Test Build and Execution**:
  ```bash
  cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
  ```
  Test binary: `build-tests/test/kites_test`.

## Development Rules & Constraints
1. **One feature per branch**: Keep logical changes isolated on designated topic branches.
2. **One commit per working change**: Atomic, well-explained commits.
3. **Continuous verification**: Build and run tests after every change.
4. **Core logic isolation**: Do NOT modify simulator core logic (RVSS/RV5S models, cache hierarchy, assembler semantics) during UI tasks unless explicitly requested.
5. **Clean compiler output**: New code must be warning-free under `-Wall -Wextra`.
6. **No premature renaming**: Do not rename existing `Kites` identifiers, namespaces, or files until an explicit dedicated rename commit.
7. **Dependency discipline**: Always ask the user before introducing any new external dependencies or libraries.
8. **Safety & Non-destructive operations**: Never execute destructive git commands (`git push`, `git reset --hard`, `git clean`) or delete files outside build directories without explicit user confirmation.

## Known Baseline Issues (Audit Findings)
1. **Windows Header Portability**: `test/test_main.cpp` originally included `<windows.h>` unconditionally. Guarded with `#ifdef _WIN32 ... #endif` on Linux.
2. **Undefined Behavior in `getDiagnostics`**: In `src/assembler/assembler.cpp` (lines 135-154), the body of `getDiagnostics` is commented out with no return statement (`-Wreturn-type`). It has zero callers across the codebase and the editor does not depend on it (live editor diagnostics currently use an internal empty vector in `LanguageService`).
3. **Build Warnings**: 23 compiler and linker warning lines under GCC 16 on Fedora 44 (detailed in `docs/audit/baseline_warnings.txt`).
4. **Test Suite Baseline**:
   - Total active tests registered: **158** (CTest and `kites_test --gtest_list_tests`).
   - Results: **43 passed**, **115 failed (SegFault)**, **0 skipped**.
5. **Disabled Tests (18 total)**:
   - **9 Commented-out `TEST` macros**: 6 in `test/rv5s_test.cpp`, 1 in `test/rv5s_h_f_test.cpp`, 1 in `test/rv5s_h_nf_test.cpp`, 1 in `test/rv5s_nh_f_test.cpp`.
   - **9 `#if 0` blocks**: 3 in `test/cache_policies_test.cpp`, 1 in `test/cache_test.cpp`, 2 in `test/pseudo_inst_test.cpp`, 3 in `test/rv5s_f_extension_test.cpp`.
