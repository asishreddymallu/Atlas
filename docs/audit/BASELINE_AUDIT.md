> Historical record. Paths in this document are as of 2026-11-02; see [docs/ARCHITECTURE.md](../ARCHITECTURE.md) for the current layout and [docs/audit/path_mapping.md](path_mapping.md) for old to new paths.

# Atlas Baseline Audit Report

- **Date**: 2026-11-02
- **Platform**: Fedora Linux 44 (Workstation Edition), x86_64
- **Project**: Atlas (RISC-V Teaching Platform, based on Kites v1.1.8 / commit `7ddb6f3`)

---

## 1. Environment & Toolchain

All required dependencies were verified using `rpm -q`:

| Package | Installed Version |
| :--- | :--- |
| `gcc-c++` | `16.2.1-2.fc44.x86_64` (GCC 16.2.1 20260819) |
| `cmake` | `4.3.0-1.fc44.x86_64` |
| `ninja-build` | `1.13.2-2.fc44.x86_64` |
| `git` | `2.56.0-1.fc44.x86_64` |
| `qt6-qtbase-devel` | `6.11.2-3.fc44.x86_64` |
| `qt6-qttools-devel` | `6.11.2-1.fc44.x86_64` |
| `qt6-qtsvg-devel` | `6.11.2-1.fc44.x86_64` |
| `gtest-devel` | `1.17.0-2.fc44.x86_64` |
| `gmock-devel` | `1.17.0-2.fc44.x86_64` |
| `lua-devel` | `5.4.8-5.fc44.x86_64` |

---

## 2. Git & Repository Verification

1. **Remotes**:
   - `upstream`: `https://github.com/hghit-harshit/Kites.git` (upstream Kites).
   - `origin`: `https://github.com/asishreddymallu/Atlas.git` (Atlas repository).
2. **Commit History**:
   - Commit `7ddb6f3` (`feat: add debug run functionality and associated UI elements`, tag `v1.1.8`) is present in history.
3. **License**:
   - File exists at `LICENSE.md`.
   - License type: **MIT License** (Copyright (c) 2025 Atharva Gajendra Lohare, Harshit Daheriya).
4. **.gitignore**:
   - Originally covered only `build/`; updated to `build*/` to encompass `build-tests/` and other build variants on branch `chore/gitignore`.
5. **README Credit**:
   - Added credit line `"Atlas is built on Kites by hghit-harshit"` on branch `chore/readme-credit`.

---

## 3. Baseline Build Results & Warnings

- **Configuration**: `cmake -S . -B build -G Ninja` succeeded cleanly.
- **Build**: `cmake --build build` succeeded cleanly.
- **Artifacts Verified**:
  - `build/Kites` (GUI executable, 6,548,952 bytes)
  - `build/kites-cli` (CLI executable, 5,838,784 bytes)
- **Warning Count**: **23 lines** containing `warning:` captured in `docs/audit/baseline_warnings.txt`.
- **Warning Categories**:
  - Unused variables: `src/common/instructions.cpp:1779` (`fromInt`).
  - Missing return statement: `src/assembler/assembler.cpp:154` (`-Wreturn-type` in `getDiagnostics`).
  - Unused parameters: `src/assembler/assembler.cpp:135`, `src/ui/cache_tab/cachemodel.cpp:232`, `src/ui/profiler_tab/profilertab.cpp:58`, `src/ui/profiler_tab/profiler_editor.cpp:15`, `src/ui/editor_tab/break_point_gutter_column.h:112`, `src/ui/register_table/registermodel.cpp:188, 197`.
  - Type limits: `src/assembler/parse_formats/pseudo_formats.cpp:210` (unsigned `>= 0`).
  - Preprocessor syntax: `src/ui/editor_tab/editortab.h:76` (extra tokens `#endif EDITORTAB__H`).
  - Structured binding range-for copy: `src/ui/editor_tab/disassembly_editor.h:38`, `src/ui/editor_tab/main_editor.cpp:92`.
  - Unused set variable: `src/ui/compiler_tab/compilertab.cpp:234` (`inTextSection`).
  - Implicit switch fallthrough: `src/ui/register_table/registermodel.cpp:160`.
  - Deprecated library function: `tmpnam` linker warning in Lua's `loslib.c` (linked in `Kites` and `kites-cli`).

---

## 4. Portability & Harness Findings

### `<windows.h>` Include in `test/test_main.cpp`
- **Issue**: `test/test_main.cpp` line 2 included `<windows.h>` unconditionally. On Linux environments, `<windows.h>` is not found, causing test builds (`cmake --build build-tests`) to fail during compilation.
- **Resolution**: Guarded `#include <windows.h>` with `#ifdef _WIN32 ... #endif` on branch `fix/test-portability`. The Windows API call `SetConsoleOutputCP(65001)` in `main()` was already guarded. No Windows code was removed.

### Headless `QApplication` & Working Directory Resolution
- **Root Cause 1 (QApplication)**: `test/test_main.cpp` originally did not instantiate a `QApplication`. Constructing any RVSS or RV5S processor initializes circuit visual components that construct a `QGraphicsScene`, which segfaults in `QGraphicsScenePrivate::init()` when no `QApplication` exists. Resolved in `fix/test-qapplication` by initializing a headless `QApplication` with platform plugin `offscreen` when no display server is present.
- **Root Cause 2 (Working Directory)**: Test cases load sample assembly files via relative paths such as `"../examples/..."` (~100 occurrences). When executed, the process working directory must be `test/`. CTest sets this working directory automatically via `gtest_discover_tests(kites_test ... WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")`. Running the `kites_test` binary directly from the repo root fails because the relative path fails to resolve.
- **Conclusion**: There are no simulator logic bugs responsible for earlier test crashes. Under CTest, all tests execute properly.

### `getDiagnostics` in `src/assembler/assembler.cpp`
- **Issue**: In `src/assembler/assembler.cpp` (lines 135–154), `getDiagnostics(std::istream &source, const std::string &virtualFilename)` has its entire body commented out. It returns `std::vector<ParseError>` without a return statement, causing undefined behavior in C++ and a `-Wreturn-type` compiler warning.
- **Callers & Call Graph**: Audited the entire codebase (`src/`, `cli/`, `test/`). **Zero callers** exist for `getDiagnostics`. It is declared only in `src/assembler/assembler.h:56`.
- **Editor Dependency**: The editor does **NOT** depend on `getDiagnostics`.
  - Live editor diagnostics are triggered in `EditorTab` via `LanguageService::computeDiagnostics(QString source)` (`src/language_service/language_service.cpp`), which initializes an internal empty `std::vector<ParseError>` without calling `getDiagnostics`.
  - Run/Debug assembly is handled via `MainWindow::tryParseAndLoadProgram()` (`src/ui/mainwindow/mainwindow.cpp`), catching exceptions and reading errors from `globals::errors_dump_file_path`.
- **Status**: Left unmodified per ground rules.

---

## 5. Test Suite Verification & Audit

- **Configuration**: `cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON`
- **Build**: `cmake --build build-tests`
- **Execution Command**: `ctest --test-dir build-tests`
- **Test Results**:
  - **Total Active Tests**: **158** (the earlier claim of 126 was incorrect).
  - **Passed**: **158**
  - **Failed**: **0**
  - **Skipped**: **0**
  - **Pass Rate**: **100% (158/158 passing)**

---

## 6. Disabled Tests Inventory (18 Total)

### A. Commented-out `TEST` macros (9 tests)
1. `test/rv5s_h_f_test.cpp:6`: `// TEST(RV5StageVM_H_F_TEST,hazard_forwarding_test_1)`
2. `test/rv5s_h_nf_test.cpp:6`: `// TEST(RV5StageVM_H_NF_TEST,hazard_forwarding_test_1)`
3. `test/rv5s_nh_f_test.cpp:7`: `// TEST(RV5S_NH_F_TEST,no_hazard_forwarding_test_1)`
4. `test/rv5s_test.cpp:11`: `// TEST(FIVE_STAGE_VM_TEST,no_hazard_no_forwarding_test_1)`
5. `test/rv5s_test.cpp:24`: `// TEST(FIVE_STAGE_VM_TEST,no_hazard_no_forwarding_test_2)`
6. `test/rv5s_test.cpp:35`: `// TEST(FIVE_STAGE_VM_TEST,hazard_no_forwarding_test_1)`
7. `test/rv5s_test.cpp:46`: `// TEST(FIVE_STAGE_VM_TEST,no_hazard_forwarding_test)`
8. `test/rv5s_test.cpp:56`: `// TEST(FIVE_STAGE_VM_TEST,harzard_no_forwarding_test)`
9. `test/rv5s_test.cpp:66`: `// TEST(FIVE_STAGE_VM_TEST,harzard_forwarding_test)`

### B. Disabled via `#if 0` blocks (9 tests)
1. `test/cache_policies_test.cpp:148-167`: `TEST_F(LRUTest, BasicEvictionOrder)`
2. `test/cache_policies_test.cpp:173-195`: `TEST_F(LRUTest, AccessPromotesToMostRecent)`
3. `test/cache_policies_test.cpp:282-311`: `TEST_F(LRUTest, LRUAgeingAcrossManyCycles)`
4. `test/cache_test.cpp:372-386`: `TEST_F(ReplacementPolicyTest, FIFOEvictsFirstInserted)`
5. `test/pseudo_inst_test.cpp:39-55`: `TEST_F(CustomPseudoManagerTest, AddPseudoInstructionAndExpandSingleLine)`
6. `test/pseudo_inst_test.cpp:57-73`: `TEST_F(CustomPseudoManagerTest, ExpandPseudoInstructionWithMultipleExpansionLines)`
7. `test/rv5s_f_extension_test.cpp:329-334`: `TEST(FExt_MixedHazard_H_F, mixed_gpr_fpr_hazard)`
8. `test/rv5s_f_extension_test.cpp:372-381`: `TEST(FExt_CompareHazard_H_F, compare_hazard)`
9. `test/rv5s_f_extension_test.cpp:500-506`: `TEST(FExt_ConvertHazard_H_F, convert_hazard)`

### C. `DISABLED_` prefixes
- **0** found across the test directory.

---

## 7. Branches and Baseline Commits

| Branch | Commit Hash | Description |
| :--- | :--- | :--- |
| `chore/gitignore` | `f256861` | Extended `.gitignore` to cover all build directories (`build*/`) |
| `chore/readme-credit` | `93977b6` | Added credit line: "Atlas is built on Kites by hghit-harshit" |
| `fix/test-portability` | `88e473a` | Guarded `<windows.h>` with `#ifdef _WIN32` in `test/test_main.cpp` |
| `fix/test-qapplication` | `bdc73f9` | Added headless `QApplication` in `test/test_main.cpp` |
| `chore/agent-context` | `aaf7bd6` | Added `AGENTS.md` and `GEMINI.md` context files |
| `docs/baseline-corrections` | *current* | Verified facts: 158/158 tests passing under CTest |
