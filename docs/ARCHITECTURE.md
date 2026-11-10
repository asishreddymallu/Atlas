# Atlas Architecture & Repository Layout

The living description of where things are and why. Generated from the tracked
tree (`git ls-files`); every path below was checked against it.

Atlas is a RISC-V teaching platform built on the Kites simulator (upstream
`7ddb6f3`, v1.1.8). C++20, Qt 6, CMake + Ninja.

For paths as they were before the `refactor/repo-layout` work, see
[docs/audit/path_mapping.md](audit/path_mapping.md).

---

## Naming convention now in force

Matches [docs/coding_convention.md](coding_convention.md):

| Thing | Convention | Example |
|---|---|---|
| Folders | `snake_case` | `src/ui/cache_tab` |
| Files | `snake_case` | `src/ui/cache_tab/cache_config_widget.cpp` |
| Classes / structs | `PascalCase` | `CacheConfigWidget` |

The `refactor/repo-layout` work brought **file names** into line with that
table. **Class names, namespaces, header guards, Qt object names and QSettings
keys were deliberately left alone** — `CacheConfigWidget` is still
`CacheConfigWidget`, now declared in `cache_config_widget.h`. Renaming
identifiers is a separate change with a different risk profile, and the `.ui`
files embed class and object names that Qt resolves at runtime.

A `.ui` rename also changes the AUTOUIC-generated header, from `ui_<old>.h` to
`ui_<new>.h`.

### Why `src/` is the include root

`CMakeLists.txt` adds `${SRC_DIR}` to `target_include_directories(kites_core
PUBLIC ...)`, so every header is included by its path **relative to `src/`**:

```cpp
#include "ui/main_window/main_window.h"   // from main.cpp
#include "processor/processor_manager.h"
```

Files inside the same directory include each other by bare name
(`#include "cache_config_widget.h"`). Two consequences: moving a directory
changes every rooted include that names it, and `CMAKE_AUTOUIC_SEARCH_PATHS` is
set to `${SRC_DIR}/ui` — the parent of all the UI subdirectories — so adding or
moving a UI subdirectory needs no CMake edit.

The source lists are `file(GLOB_RECURSE ...)` over `${SRC_DIR}` **without**
`CONFIGURE_DEPENDS`, so after adding, moving or removing a file you must re-run
`cmake` rather than rely on an incremental reconfigure.

---

## Top level

| Path | Purpose |
|---|---|
| `main.cpp` | GUI entry point; constructs `Kites::MainWindow`. |
| `CMakeLists.txt`, `CMakePresets.json` | Build definition for `Kites` (GUI), `kites-cli`, and `kites_core` (static library both link). |
| `src/` | All application code. Also the include root. |
| `cli/` | Command-line front end: `main.cpp`, `repl.cpp`, `repl.h`. |
| `test/` | GoogleTest suite (28 `.cpp` files + `test/CMakeLists.txt`). |
| `examples/` | RISC-V assembly sample programs used by the tests and the UI. |
| `resources/` | Qt resources: themes, icons, fonts, circuit designs, instruction metadata. |
| `external/` | Vendored third-party code (`external/lua-5.5.0`), left untouched by the layout work. |
| `docs/` | This document and the audit/design records. |
| `scripts/` | `build.sh`, `test.sh` convenience wrappers. |
| `.github/` | CI workflow. |
| `Doxyfile`, `.clang-format`, `.clangd` | Tooling configuration. |
| `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, `CONTRIBUTING.md`, `README.md`, `LICENSE.md` | Project and agent-facing documentation. |

---

## `src/` subtrees

| Path | Purpose |
|---|---|
| `src/assembler/` | Lexer, parser, code generator and error types; the `parse_formats/` files (`i_m_formats.cpp`, `f_d_formats.cpp`, `csr_formats.cpp`, `pseudo_formats.cpp`) hold the per-instruction-family parse rules. |
| `src/processor/` | Simulator core: ALU, registers, main memory, memory controller, processor factory and `ProcessorManager`. |
| `src/processor/rvss/` | Single-cycle processor model and its control unit. |
| `src/processor/rv5s/` | Five-stage pipelined model, in four variants by hazard-detection and forwarding (`h_f`, `h_nf`, `nh_f`, `nh_nf`), plus the hazard detection unit. |
| `src/processor/cache/` | Cache hierarchy, configuration, and replacement policies (`fifo`, `lru`, and the Lua-backed `custom_policy` engine). |
| `src/common/` | Shared value types: `assembled_program.h`, `instructions.*`, `register_names.h`, `diagnostic_severity.h`, `undo_buffer.h`, and `globals.*` (runtime file paths). |
| `src/language_service/` | Editor-facing language support: the `Diagnostic` model (`diagnostic.h/.cpp`), `LanguageService`, and `CompletionProvider` (static RV32/64 autocomplete data). |
| `src/config/` | `config.*` (assembler configuration options) and `app_settings.*` (persisted UI preferences — theme, font, panel state). |
| `src/custom_pseudo_manager/` | Expands **user-defined** pseudo-instructions in the source text before assembly. Built-in `li`/`la`/`call` are expanded inside the parser, not here. |
| `src/profiler/` | `Profiler`: maps instructions to source lines and accumulates per-line execution counts and instruction types as the processor is clocked. |
| `src/command_handler/` | `Command`/`ReplState` types and `ExecuteCommand`, driving the CLI REPL against a `ProcessorManager`. |
| `src/elf_util/` | ELF header/section structs and `generateElfFile()` for writing an assembled program out as ELF. |
| `src/utils/` | File I/O and string helpers; `to_index.h`. |
| `src/ui/` | All Qt widgets. One subdirectory per tab, plus shared pieces. |

### `src/ui/` subtrees

| Path | Purpose |
|---|---|
| `src/ui/main_window/` | `MainWindow` — hosts the tabs in a `QStackedWidget`, owns the toolbar, menubar, sidebar, status bar and Problems dock, and drives assembly and run. |
| `src/ui/editor_tab/` | Source editor tab: `main_editor.*` (the editor widget), `disassembly_editor.h`, `break_point_gutter_column.h`, and `editor_tab.*` which owns the Input/Expanded panes and the diagnostic squiggles. |
| `src/ui/memory_tab/` | Memory view: `memory_model.*`, `memory_table_view.*`, `memory_tab.*`. |
| `src/ui/register_table/` | Register panel: `register_model.*` and `register_container.*`. |
| `src/ui/cache_tab/` | Cache view: `cache_model.*`, `cache_grid_delegate.h`, `cache_config_widget.*` (one per cache level) and `cache_tab.*`. |
| `src/ui/processor_tab/` | Datapath view: `circuit_scene.*`, `circuit_view.*`, `vm_state_table_model.*`, and `processor_designs/` holding the per-model scene definitions and the drawable components (`alu_item.h`, `mux_item.h`, `wire_item.*`, …). |
| `src/ui/profiler_tab/` | Profiler view: `profiler_editor.*`, `instruction_type_count_model.*`, and the execution-count / instruction-type gutter columns. |
| `src/ui/compiler_tab/` | C-to-assembly compiler front end (`compiler_tab.*`) and its read-only `compiler_editor.h`. |
| `src/ui/problems_panel/` | `ProblemsModel` and `ProblemsDock` — the diagnostics list, with double-click navigation back into the editor. |
| `src/ui/log_panel/` | A plain text edit used for debug logging. |
| `src/ui/dialogs/` | `settings_dialog.*`, `processor_dialog.*`, `addpseudo_dialog.*` (each with a `.ui`). |
| `src/ui/theme/` | `ThemeManager` (loads the theme JSON and renders `template.qss`), `FontManager`, `IconManager`, and `app_style.h` (a `QProxyStyle`). |
| `src/ui/common/` | Shared UI pieces: `kites_tab.*` (tab base class), `kites_editor.*`, `syntax_highlighter.*`, `line_number_gutter_column.*`, `gutter_column.h`, `collapsible_section.*`, `data_table_style.*`, `table_font.*`, `display_base_types.h`. |

---

## How to find X

| Looking for | Path |
|---|---|
| Lexer / parser / code generator | `src/assembler/` — `lexer.cpp`, `parser.cpp`, `code_generator.cpp` |
| Per-instruction parse rules | `src/assembler/parse_formats/` |
| The diagnostic model (severity, category, column span) | `src/language_service/diagnostic.h`, `src/common/diagnostic_severity.h` |
| Column convention (1-based, exclusive end, 0 = unknown) | the doc comment in `src/common/diagnostic_severity.h` |
| The Problems dock | `src/ui/problems_panel/problems_dock.cpp`, model in `problems_model.cpp` |
| Editor squiggles and diagnostic navigation | `src/ui/editor_tab/editor_tab.cpp` |
| A given tab's widget | `src/ui/<name>_tab/<name>_tab.cpp` (e.g. `src/ui/cache_tab/cache_tab.cpp`); the register panel is the exception, at `src/ui/register_table/register_container.cpp` |
| The main window, toolbar, menus | `src/ui/main_window/main_window.cpp` |
| Theme colours | `resources/themes/*.json` — `tokyo_night.json`, `tokyo_night_storm.json`, `kites_dark.json`, `kites_light.json` |
| The stylesheet template | `resources/themes/template.qss` |
| Theme loading / token substitution | `src/ui/theme/theme_manager.cpp` |
| Instruction metadata (autocomplete, help) | `resources/instructions/instructions.json` |
| Cache replacement policies | `src/processor/cache/` — `fifo.cpp`, `lru.cpp`, `custom_policy.cpp` |
| Writing a custom (Lua) replacement policy | `docs/CUSTOM_REPLACEMENT_POLICY.md`; samples in `examples/custom-replacement-scripts/` |
| Pipelined processor variants | `src/processor/rv5s/rv5s_processor_{h_f,h_nf,nh_f,nh_nf}.cpp` |
| Single-cycle processor | `src/processor/rvss/rvss_processor.cpp` |
| Tests | `test/` — run with `ctest --test-dir build-tests`; see `docs/TESTING.md` |
| Which tests are disabled and why | `docs/audit/disabled_tests.md` |
| Known defects left unfixed | `docs/audit/known_issues.md` |
| Old path → new path after the layout refactor | `docs/audit/path_mapping.md` |
| CLI usage | `docs/CLI.md`; code in `cli/` and `src/command_handler/` |
| Sample programs | `examples/` |
| The custom-pseudo-instruction JSON | **Not tracked** — written at runtime to `<writable-data-dir>/vm_state/custom_pseudo_instructions.json`; the path is built in `src/common/globals.cpp`. The expander is `src/custom_pseudo_manager/custom_pseudo_manager.cpp`. |
| Other runtime state (disassembly, dumps, config.ini) | **Not tracked** — `<writable-data-dir>/vm_state/`, paths defined in `src/common/globals.cpp` |

---

## Build and test

```bash
cmake -S . -B build -G Ninja && cmake --build build
cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON && cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Binaries: `build/Kites` (GUI) and `build/kites-cli` (CLI).

Tests must be run through CTest, not by invoking the test binary directly:
they open example programs by relative path and CTest sets the working
directory for them. See `docs/TESTING.md`.
