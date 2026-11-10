> Pre-refactor survey (historical). The living document is docs/ARCHITECTURE.md.

# Repository Layout Plan (Phase 0 survey)

Branch `refactor/repo-layout`, from `feature/diagnostics-problems-panel` at tag
`pre-reorg-v0` (`b8c690d`). Baseline: **225/225 tests pass**. Read-only survey;
nothing moved yet.

---

## 1. Current tree, with tracked-file counts

```
.                         14   main.cpp, CMakeLists.txt, 4 agent/doc files, Doxyfile, presets, "100"
├── cli                    3   kites-cli entry point + repl
├── docs                   4   coding_convention, TESTING, CLI, CUSTOM_REPLACEMENT_POLICY
│   ├── audit              4   BASELINE_AUDIT, known_issues, disabled_tests, baseline_warnings
│   └── design             1   editor_ui_exploration
├── examples              28   .s/.asm test programs
│   ├── custom-replacement-scripts  1
│   └── fd-extension      16
├── external/lua-5.5.0    75   vendored Lua (2 + doc 10 + src 63)
├── .github/workflows      1   release.yml
├── resources              1   resources.qrc
│   ├── circuit_designs    9   processor diagram JSON
│   ├── fonts              7   IBM Plex Mono, Monaco
│   ├── icons             22   svg/png + app_icon.rc
│   ├── instructions       2   instructions.json
│   └── themes             6   template.qss + 4 theme JSON
├── scripts                2   build.sh, test.sh
├── src
│   ├── assembler         12   lexer, parser, assembler, errors, code_generator
│   │   └── parse_formats  4   i_m / f_d / csr / pseudo format parsers
│   ├── command_handler    2
│   ├── common            11   globals, assembled_program, diagnostic_severity
│   ├── config             4
│   ├── custom_pseudo_manager  2
│   ├── elf_util           2
│   ├── language_service   6   language_service, diagnostic, completion_provider
│   ├── processor         23   + cache 12 + rv5s 14 + rvss 4
│   ├── profiler           2
│   ├── ui
│   │   ├── cache_tab      9      compiler_tab 4    dialogs 9
│   │   ├── common        16      editor_tab 7      log_panel 2
│   │   ├── mainwindow     3      memory_tab 7      problems_panel 4
│   │   ├── processor_tab 26      profiler_tab 9    register_table 5
│   │   └── theme          7
│   └── utils              3
└── test                  29   GoogleTest suites + test_main.cpp
```

Total tracked: **434 files**.

---

## 2. How things are found today

### CMake
| Mechanism | Detail |
|---|---|
| `SRC_DIR` | `"src"` (relative), `EXTERNAL_DIR` `"external"` |
| Source discovery | `file(GLOB_RECURSE SRC_FILES "${SRC_DIR}/*.cpp")`, same for `*.h` and `*.ui` — **no `CONFIGURE_DEPENDS`**, so adding/moving files needs a manual re-configure |
| Lua | `file(GLOB … CONFIGURE_DEPENDS "${EXTERNAL_DIR}/lua-5.5.0/src/*.c")`, excluding `lua.c`/`luac.c`, compiled with `-Wno-pedantic` |
| Include path | `target_include_directories(kites_core PUBLIC "${PROJECT_BINARY_DIR}/${PROJECT_NAME}_autogen/include" ${SRC_DIR} ${EXTERNAL_DIR}/lua-5.5.0/src)` — **`src/` is the include root** |
| Targets | `kites_core` (STATIC), `Kites` (OUTPUT_NAME `Kites`), `kites_cli` (OUTPUT_NAME `kites-cli`) |
| Tests | `test/CMakeLists.txt`, `file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS "*.cpp")`, `gtest_discover_tests(... WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/test)` |
| Qt | `CMAKE_AUTOMOC ON`, `CMAKE_AUTOUIC ON`, `CMAKE_AUTOUIC_SEARCH_PATHS = src/ui`. AUTORCC is **not** used; `.qrc` go through `qt6_add_resources` |

**Defect found (not to be fixed here):** line 42 globs `"${TEST_DIR}/*.cpp"` but `TEST_DIR`
is never set in the root `CMakeLists.txt`, so it expands to `"/*.cpp"`. The
resulting `TEST_FILES` variable is never used. Harmless today; reported, not touched.

### Resources
`qt6_add_resources` is given six `.qrc` by **relative path** from the root.
`resources/resources.qrc` additionally lists the other five `.qrc` as *files*
(not as includes). Prefixes: `/circuit_designs`, `/fonts`, `/icons`,
`/instructions`, `/themes`. Code references 22 distinct `:/…` paths. **Resource
prefixes are independent of directory layout** — moving `resources/` only
requires updating the six paths in `CMakeLists.txt`.

### Runtime paths
`src/common/globals.cpp` builds nine paths under `invokation_path / "vm_state"`:
`config.ini`, `disassembly.txt`, `temp.asm`, `errors_dump.json`,
`registers_dump.json`, `memory_dump.json`, `cache_dump.json`,
`vm_state_dump.json`, `custom_pseudo_instructions.json`. `invokation_path` is
`std::filesystem::current_path()` on Linux/Windows. `vm_state/` is **gitignored**
and created at runtime — it is not part of the tree to move.

### `.ui` files → generated headers
AUTOUIC turns `foo.ui` into `ui_foo.h`. **Renaming a `.ui` changes the generated
header name and the `#include` in its `.cpp`.** All twelve, with the class each backs:

| .ui | generates | class | included by |
|---|---|---|---|
| `cache_tab/cacheconfigwidget.ui` | `ui_cacheconfigwidget.h` | `Kites::CacheConfigWidget` | `cacheconfigwidget.cpp` |
| `cache_tab/cachetab.ui` | `ui_cachetab.h` | `Kites::CacheTab` | `cachetab.cpp` |
| `compiler_tab/compilertab.ui` | `ui_compilertab.h` | `Kites::CompilerTab` | `compilertab.cpp` |
| `dialogs/addpseudo_dialog.ui` | `ui_addpseudo_dialog.h` | `Kites::AddPseudoDialog` | `addpseudo_dialog.cpp` |
| `dialogs/processor_dialog.ui` | `ui_processor_dialog.h` | `Kites::ProcessorDialog` | `processor_dialog.cpp` |
| `dialogs/settings_dialog.ui` | `ui_settings_dialog.h` | `Kites::SettingsDialog` | `settings_dialog.cpp` |
| `editor_tab/editortab.ui` | `ui_editortab.h` | `Kites::EditorTab` | `editortab.cpp` |
| `mainwindow/mainwindow.ui` | `ui_mainwindow.h` | `MainWindow` | `mainwindow.cpp` |
| `memory_tab/memorytab.ui` | `ui_memorytab.h` | `Kites::MemoryTab` | `memorytab.cpp` |
| `processor_tab/processortab.ui` | `ui_processortab.h` | `Kites::ProcessorTab` | `processortab.cpp` |
| `profiler_tab/profilertab.ui` | `ui_profilertab.h` | `Kites::ProfilerTab` | `profilertab.cpp` |
| `register_table/registercontainer.ui` | `ui_registercontainer.h` | `Kites::RegisterContainer` | `registercontainer.cpp` |

`docs/coding_convention.md` opens with: **"DO NOT REFORMAT THE UI FILES IT WILL
BREAK THINGS UNEXPECTEDLY."** A `git mv` does not reformat, but any `.ui` rename
must be paired with its `#include "ui_*.h"` edit in the same commit.

### Includes
- rooted (`"module/file.h"`): **438**
- bare (`"file.h"`, same directory): **126**
- relative `"../file.h"`: **6** — `rvss/rvss_control_unit.h` and the five
  `processor_designs/*_circuit_scene.h`
- `.hpp`: 1, and it is `external/lua-5.5.0/src/lua.hpp` (vendored)

Bare includes resolve because the file sits beside its includer; rooted ones
resolve because `src/` is on the include path. **Moving a directory breaks every
rooted include that names it, and every `../` include that crosses it.**

### Tests
11 test files open fixtures as `"../examples/…"`, **104 occurrences**. These work
only because `gtest_discover_tests` sets `WORKING_DIRECTORY` to `test/`.
Moving either `test/` or `examples/` changes that relationship.

### Scripts, CI, packaging, docs
- `scripts/build.sh`, `scripts/test.sh`: `cd` to repo root, then `cmake -S . -B build[-tests]`. **No source paths** — immune to the reorg.
- `.github/workflows/release.yml`: references `build/Kites`, `build/Kites.exe`, `build/Kites.app` — **build outputs only**, immune.
- `Doxyfile`: `INPUT`, `OUTPUT_DIRECTORY`, `STRIP_FROM_PATH` are all **empty** — immune.
- `CMakePresets.json`: only Qt/MinGW paths — immune.
- `.clangd`: `CompilationDatabase: build/` — immune.
- Path citations: `docs/design/editor_ui_exploration.md` **61**, `docs/audit/BASELINE_AUDIT.md` **37**, `docs/audit/disabled_tests.md` **18**, `AGENTS.md` **10**, `docs/CLI.md` **7**, `docs/audit/known_issues.md` **7**, `Doxyfile` **7**, `GEMINI.md` **3**.

---

## 3. Naming rules

`docs/coding_convention.md` already mandates:

> Folders `snake_case` · Files `snake_case` · organise by **feature/domain, not
> file type** · keep related `.h`/`.cpp`/`.ui` together · GUI under `src/ui/` ·
> shared GUI under `src/ui/common/`

**The existing directory structure already satisfies the structural rules.** The
only systematic violation is **file naming**: 35 files use glued multi-word stems
instead of `snake_case`. No file uses uppercase. Full list:

| Directory | Files violating `snake_case` |
|---|---|
| `src/ui/cache_tab` | `cacheconfigwidget.{cpp,h,ui}`, `cachemodel.{cpp,h}`, `cachetab.{cpp,h,ui}` |
| `src/ui/common` | `kitestab.{cpp,h}` |
| `src/ui/compiler_tab` | `compilertab.{cpp,h,ui}` |
| `src/ui/editor_tab` | `editortab.{cpp,h,ui}` |
| `src/ui/mainwindow` | `mainwindow.{cpp,h,ui}` + the directory itself |
| `src/ui/memory_tab` | `memorymodel.{cpp,h}`, `memorytab.{cpp,h,ui}` |
| `src/ui/processor_tab` | `processortab.{cpp,h,ui}` |
| `src/ui/profiler_tab` | `profilertab.{cpp,h,ui}` |
| `src/ui/register_table` | `registercontainer.{cpp,h,ui}`, `registermodel.{cpp,h}` |

No proposal is needed — the convention exists; these files predate it.

---

## 4. Proposed target tree

**The directory layout does not need to change.** It already matches the
convention's structural rules. The proposal is therefore *naming only*, plus two
new documentation files:

```
src/
├── assembler/          lexer, parser, code generation, error types
│   └── parse_formats/  per-instruction-format parse routines
├── command_handler/    CLI command dispatch
├── common/             globals, shared value types, diagnostic severity
├── config/             persisted application settings
├── custom_pseudo_manager/  user-defined pseudo-instruction expansion
├── elf_util/           ELF reading helpers
├── language_service/   editor-facing diagnostics and completion
├── processor/          VM core; cache/, rv5s/, rvss/ hold the models
├── profiler/           per-line execution statistics
├── ui/
│   ├── common/         widgets shared by tabs (editor base, gutters, tables)
│   ├── main_window/    shell: sidebar, toolbar, menus, docks   [renamed]
│   ├── <feature>_tab/  one directory per tab
│   ├── problems_panel/ Problems dock model and widget
│   ├── dialogs/        modal dialogs
│   ├── log_panel/      optional debug log pane
│   └── theme/          theme, font and icon managers
└── utils/              small free-standing helpers
docs/
├── ARCHITECTURE.md     final tree, what lives where, rules for new files  [new]
└── audit/path_mapping.md   permanent old -> new table                     [new]
```

Everything else (`cli/`, `test/`, `resources/`, `examples/`, `scripts/`,
`external/`, `.github/`) stays exactly where it is.

---

## 5. Old → new mapping

**35 file renames + 1 directory rename.** Every row is a rename that requires
edits in the same commit. No pure moves are proposed.

| # | Old | New | Kind | Edits needed |
|---|---|---|---|---|
| 1-3 | `src/ui/cache_tab/cacheconfigwidget.{cpp,h,ui}` | `cache_config_widget.{cpp,h,ui}` | rename + generated-name change | `#include "ui_cacheconfigwidget.h"`, `#include "cacheconfigwidget.h"` in `cachetab.cpp` |
| 4-5 | `src/ui/cache_tab/cachemodel.{cpp,h}` | `cache_model.{cpp,h}` | rename | includes in `cachetab.cpp`, `cache_grid_delegate.h` |
| 6-8 | `src/ui/cache_tab/cachetab.{cpp,h,ui}` | `cache_tab.{cpp,h,ui}` | rename + generated-name change | `ui_cachetab.h`; `mainwindow.cpp` include |
| 9-10 | `src/ui/common/kitestab.{cpp,h}` | `kites_tab.{cpp,h}` | rename | 7 rooted includes across tabs |
| 11-13 | `src/ui/compiler_tab/compilertab.{cpp,h,ui}` | `compiler_tab.{cpp,h,ui}` | rename + generated-name change | `ui_compilertab.h`; `mainwindow.cpp` |
| 14-16 | `src/ui/editor_tab/editortab.{cpp,h,ui}` | `editor_tab.{cpp,h,ui}` | rename + generated-name change | `ui_editortab.h`; `mainwindow.cpp`, 2 test files |
| 17-19 | `src/ui/mainwindow/mainwindow.{cpp,h,ui}` | `src/ui/main_window/main_window.{cpp,h,ui}` | dir rename + file rename + generated-name change | `ui_mainwindow.h`; `main.cpp`, 3 scratch harnesses (untracked) |
| 20-21 | `src/ui/memory_tab/memorymodel.{cpp,h}` | `memory_model.{cpp,h}` | rename | `memorytab.cpp`, `memory_table_view.cpp` |
| 22-24 | `src/ui/memory_tab/memorytab.{cpp,h,ui}` | `memory_tab.{cpp,h,ui}` | rename + generated-name change | `ui_memorytab.h`; `mainwindow.cpp` |
| 25-27 | `src/ui/processor_tab/processortab.{cpp,h,ui}` | `processor_tab.{cpp,h,ui}` | rename + generated-name change | `ui_processortab.h`; `mainwindow.cpp` |
| 28-30 | `src/ui/profiler_tab/profilertab.{cpp,h,ui}` | `profiler_tab.{cpp,h,ui}` | rename + generated-name change | `ui_profilertab.h`; `mainwindow.cpp` |
| 31-33 | `src/ui/register_table/registercontainer.{cpp,h,ui}` | `register_container.{cpp,h,ui}` | rename + generated-name change | `ui_registercontainer.h`; `mainwindow.{h,cpp}` |
| 34-35 | `src/ui/register_table/registermodel.{cpp,h}` | `register_model.{cpp,h}` | rename | `registercontainer.cpp` |

`docs/design/repo_layout_plan.md` → `docs/ARCHITECTURE.md` (rewritten) and
`docs/audit/path_mapping.md` (new) are produced in the final documentation phase.

---

## 6. Risks specific to this repository

**R1 — Upstream merges (the dominant risk).** `upstream` points at
`hghit-harshit/Kites`. **348 of 434 tracked files are byte-identical to
`v1.1.8`**, including `src/processor` (53/53), `src/ui/dialogs` (9/9),
`src/ui/processor_tab` (24/26), `cli` (3/3), `examples` (45/45) and `external`
(75/75). Git 2.56 does rename detection on merge by default and the counts here
are far below any rename limit, so a *pure* move is usually followed correctly.
It stops being free when upstream edits the same file, where the conflict is
then reported against the old path and must be resolved by hand.
**Recommendation: do not move or rename any Kites-origin file.** Every row in
§5 is a file Atlas already owns or barely touches — the proposal deliberately
avoids `src/processor`, `src/ui/dialogs`, `cli/`, `examples/` and `external/`.
`src/ui/mainwindow` is 2/3 Atlas-modified, and `processortab.*` is Atlas-modified,
so both are safe to rename.

**R2 — `.ui` renames change generated header names.** Nine of the twelve `.ui`
files are renamed. Each changes `ui_<old>.h` to `ui_<new>.h`, so the `#include`
in its `.cpp` must change in the same commit or the build breaks. Mitigated by
committing rename + include edit together and building every phase.

**R3 — Glob-based CMake without `CONFIGURE_DEPENDS`.** `SRC_FILES`,
`HEADER_FILES` and `UI_FILES` do not auto-refresh. Every phase must re-run
`cmake -S . -B …` explicitly. Already observed in this session: a new file
compiled but its moc was skipped until `ParseCache.txt` was deleted.

**R4 — Stale AUTOMOC/AUTOUIC caches.** Renaming a `Q_OBJECT` header or a `.ui`
can leave `ParseCache.txt` and `<target>_autogen/` stale, producing "undefined
reference to vtable" or a missing `ui_*.h`. Verification (a) deletes both build
trees outright, which removes the class of problem.

**R5 — Bare includes.** 126 includes name a file with no directory. They survive
a rename of the *directory* but break on a rename of the *file*. All 35 rows in
§5 are file renames, so each must be grepped for both rooted and bare forms.

**R6 — `../` includes.** Six relative includes exist; none crosses a directory
proposed for renaming (`rvss/` and `processor_designs/` are untouched).

**R7 — Test fixtures.** 104 `"../examples/…"` literals depend on `test/` and
`examples/` keeping their current relationship. Neither moves.

**R8 — Scratch harnesses.** The verification harnesses in the session scratchpad
include `ui/mainwindow/mainwindow.h` and `ui/editor_tab/editortab.h`. They are
untracked and outside the repo, but they must be updated for verification (e) to
keep working after phase 3.

**R9 — `CLAUDE.md` is tracked *and* listed in `.gitignore`.** Editing it is fine;
worth knowing it will not show as untracked if ever deleted.

**R10 — Empty tracked file `100` at the repo root.** Zero bytes, no references.
Out of scope; flagged only.

---

## 7. Phased execution order

Each phase is independently buildable and testable, smallest risk first.

| Phase | Content | Rows | Risk |
|---|---|---|---|
| **1** | Docs and agent files: add `docs/audit/path_mapping.md`, update path citations in `AGENTS.md`, `GEMINI.md`, `docs/CLI.md`, `docs/audit/*` | 0 code | none — no build input |
| **2** | `src/ui/common/kitestab.{cpp,h}` → `kites_tab.{cpp,h}` | 9-10 | low; 7 includes, no `.ui` |
| **3** | Leaf models: `cachemodel`, `memorymodel`, `registermodel` | 4-5, 20-21, 34-35 | low; no `.ui` |
| **4** | Tabs with `.ui`: cache, compiler, memory, processor, profiler, register, editor | 1-8, 11-16, 22-33 | medium; 8 generated-header changes |
| **5** | `src/ui/mainwindow` → `src/ui/main_window` + file rename | 17-19 | highest; touches `main.cpp` and every tab include |
| **6** | `docs/design/repo_layout_plan.md` → `docs/ARCHITECTURE.md`, README structure section, "how to find X" table | 0 code | none |

Commands per phase:
```bash
git branch --show-current                      # must print refactor/repo-layout
git mv <old> <new>                             # one per row
# edit #include / ui_*.h references for that row only
rm -rf build build-tests
cmake -S . -B build -G Ninja && cmake --build build
cmake -S . -B build-tests -G Ninja -DENABLE_TESTS=ON && cmake --build build-tests
ctest --test-dir build-tests
git add <explicit paths>                       # never -A
git commit
```

---

## 8. Verification procedure and cost

| | Check | How | Approx. |
|---|---|---|---|
| a | Clean configure + build of both trees, zero new warnings vs `docs/audit/baseline_warnings.txt` | `rm -rf build build-tests`, reconfigure, build, diff warning set | ~6 min |
| b | `ctest` = **225/225** | `ctest --test-dir build-tests` | ~20 s |
| c | Byte identity | `git diff -M100% --stat` must show 100% similarity for pure moves; for renames with include edits, diff old vs new with `#include` and `ui_*.h` lines masked — residue must be empty | ~1 min |
| d | Behaviour baselines from `pre-reorg-v0` in a worktree: machine code 44/44, tokens 44/44, directive corpus 8/8, five broken programs 2/5 · 2/5 · 2/18 · 2/5 · 2/0, 0 `all_errors` lines changed | rebuild the six scratch harnesses against both trees | ~8 min first phase, ~3 min after |
| e | Offscreen smoke: MainWindow, four themes, load an example, run to completion, force an error and confirm Problems rows, confirm the custom pseudo file resolves | scratch harness | ~2 min |
| f | `git log --follow` on three moved files shows pre-move history | | ~10 s |
| g | Zero dangling references: grep the tree for every old path and filename | | ~30 s |

**≈ 18 minutes for the first phase, ≈ 10 minutes for each later one**; six phases,
roughly **70 minutes of verification** in total, plus edit time.

---

## Decision needed before Phase 1

The headline is that **the directory layout already matches the project's own
convention** — organise by feature, related files together, GUI under `src/ui/`.
What does not match is 35 file names. So this is a *rename* refactor, not a
*move* refactor, and the proposal is deliberately narrow to keep future
`git merge upstream/main` cheap.

If you want a deeper reorganisation (for example splitting `src/processor` or
introducing `src/core/`), say so — but note that **every such file is
Kites-origin**, and the trade is explicit: tidier tree now, manual conflict
resolution on every upstream merge later.
