# Path Mapping

Permanent record of every file moved or renamed by the `refactor/repo-layout`
work, so that older notes, commit messages and reports that cite the old paths
can still be traced.

Rollback point: tag **`pre-reorg-v0`**.

## How to read this

- **Kind** — `rename` is a same-directory name change; `dir+rename` also moves
  between directories. A `.ui` rename additionally changes the AUTOUIC-generated
  header name from `ui_<old>.h` to `ui_<new>.h`.
- **Origin** — `Kites` means the file was byte-identical to upstream `v1.1.8`
  when the reorganisation began, so upstream still owns it; `Atlas` means this
  fork had already modified it.
- **Upstream branches touching it** — branches on `hghit-harshit/Kites` that
  have changed this path since `7ddb6f3` (`v1.1.8`). A future
  `git merge upstream/<branch>` relies on git's rename detection for these;
  where upstream also edits the file, the conflict is reported against the old
  path.

`upstream/main` has **19 commits and 33 changed files** since `7ddb6f3`, all in
the assembler and processor layers. **It touches none of the 35 files below.**

## Mapping

| # | Old path | New path | Kind | Origin | Upstream branches touching it | Commit | Status |
|---|---|---|---|---|---|---|---|
| 1 | `src/ui/cache_tab/cacheconfigwidget.cpp` | `src/ui/cache_tab/cache_config_widget.cpp` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `fde8670` | Done |
| 2 | `src/ui/cache_tab/cacheconfigwidget.h` | `src/ui/cache_tab/cache_config_widget.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `fde8670` | Done |
| 3 | `src/ui/cache_tab/cacheconfigwidget.ui` | `src/ui/cache_tab/cache_config_widget.ui` | rename (+ui_*.h) | Atlas | fix/cache, 1to4firstIm, refiningmodes | `fde8670` | Done |
| 4 | `src/ui/cache_tab/cachemodel.cpp` | `src/ui/cache_tab/cache_model.cpp` | rename | Atlas | kamaldev, fix/cache, 1to4firstIm, refiningmodes | `bb16c8e` | Done |
| 5 | `src/ui/cache_tab/cachemodel.h` | `src/ui/cache_tab/cache_model.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `bb16c8e` | Done |
| 6 | `src/ui/cache_tab/cachetab.cpp` | `src/ui/cache_tab/cache_tab.cpp` | rename | Atlas | fix/cache, 1to4firstIm, refiningmodes | `5b60d23` | Done |
| 7 | `src/ui/cache_tab/cachetab.h` | `src/ui/cache_tab/cache_tab.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `5b60d23` | Done |
| 8 | `src/ui/cache_tab/cachetab.ui` | `src/ui/cache_tab/cache_tab.ui` | rename (+ui_*.h) | Atlas | fix/cache, 1to4firstIm, refiningmodes | `5b60d23` | Done |
| 9 | `src/ui/common/kitestab.cpp` | `src/ui/common/kites_tab.cpp` | rename | Kites | 1to4firstIm, refiningmodes | `4540949` | Done |
| 10 | `src/ui/common/kitestab.h` | `src/ui/common/kites_tab.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `4540949` | Done |
| 11 | `src/ui/compiler_tab/compilertab.cpp` | `src/ui/compiler_tab/compiler_tab.cpp` | rename | Atlas | fix/cache, 1to4firstIm, refiningmodes | `c129eff` | Done |
| 12 | `src/ui/compiler_tab/compilertab.h` | `src/ui/compiler_tab/compiler_tab.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `c129eff` | Done |
| 13 | `src/ui/compiler_tab/compilertab.ui` | `src/ui/compiler_tab/compiler_tab.ui` | rename (+ui_*.h) | Atlas | fix/cache, 1to4firstIm, refiningmodes | `c129eff` | Done |
| 14 | `src/ui/editor_tab/editortab.cpp` | `src/ui/editor_tab/editor_tab.cpp` | rename | Atlas | kamaldev, ui/fixes, fix/cache, 1to4firstIm, refiningmodes, linux-installer-for-applications | `cb61d5a` | Done |
| 15 | `src/ui/editor_tab/editortab.h` | `src/ui/editor_tab/editor_tab.h` | rename | Atlas | kamaldev, ui/fixes, fix/cache, 1to4firstIm, refiningmodes, linux-installer-for-applications | `cb61d5a` | Done |
| 16 | `src/ui/editor_tab/editortab.ui` | `src/ui/editor_tab/editor_tab.ui` | rename (+ui_*.h) | Atlas | fix/cache, 1to4firstIm, refiningmodes | `cb61d5a` | Done |
| 17 | `src/ui/mainwindow/mainwindow.cpp` | `src/ui/main_window/main_window.cpp` | dir+rename | Atlas | kamaldev, ui/fixes, fix/cache, 1to4firstIm, refiningmodes, linux-installer-for-applications | `767f19f`, `7f497ab` | Done |
| 18 | `src/ui/mainwindow/mainwindow.h` | `src/ui/main_window/main_window.h` | dir+rename | Atlas | kamaldev, ui/fixes, fix/cache, 1to4firstIm, refiningmodes, linux-installer-for-applications | `767f19f`, `7f497ab` | Done |
| 19 | `src/ui/mainwindow/mainwindow.ui` | `src/ui/main_window/main_window.ui` | dir+rename | Kites | 1to4firstIm, refiningmodes | `767f19f`, `7f497ab` | Done |
| 20 | `src/ui/memory_tab/memorymodel.cpp` | `src/ui/memory_tab/memory_model.cpp` | rename | Atlas | kamaldev, ui/fixes, fix/cache, 1to4firstIm, refiningmodes, linux-installer-for-applications | `e97585e` | Done |
| 21 | `src/ui/memory_tab/memorymodel.h` | `src/ui/memory_tab/memory_model.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `e97585e` | Done |
| 22 | `src/ui/memory_tab/memorytab.cpp` | `src/ui/memory_tab/memory_tab.cpp` | rename | Atlas | fix/cache, 1to4firstIm, refiningmodes | `eb86428` | Done |
| 23 | `src/ui/memory_tab/memorytab.h` | `src/ui/memory_tab/memory_tab.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `eb86428` | Done |
| 24 | `src/ui/memory_tab/memorytab.ui` | `src/ui/memory_tab/memory_tab.ui` | rename (+ui_*.h) | Kites | fix/cache, 1to4firstIm, refiningmodes | `eb86428` | Done |
| 25 | `src/ui/processor_tab/processortab.cpp` | `src/ui/processor_tab/processor_tab.cpp` | rename | Atlas | fix/cache, 1to4firstIm, refiningmodes | `c85bc9f` | Done |
| 26 | `src/ui/processor_tab/processortab.h` | `src/ui/processor_tab/processor_tab.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `c85bc9f` | Done |
| 27 | `src/ui/processor_tab/processortab.ui` | `src/ui/processor_tab/processor_tab.ui` | rename (+ui_*.h) | Kites | fix/cache, 1to4firstIm, refiningmodes | `c85bc9f` | Done |
| 28 | `src/ui/profiler_tab/profilertab.cpp` | `src/ui/profiler_tab/profiler_tab.cpp` | rename | Atlas | kamaldev, ui/fixes, fix/cache, 1to4firstIm, refiningmodes | `0285490` | Done |
| 29 | `src/ui/profiler_tab/profilertab.h` | `src/ui/profiler_tab/profiler_tab.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `0285490` | Done |
| 30 | `src/ui/profiler_tab/profilertab.ui` | `src/ui/profiler_tab/profiler_tab.ui` | rename (+ui_*.h) | Kites | fix/cache, 1to4firstIm, refiningmodes | `0285490` | Done |
| 31 | `src/ui/register_table/registercontainer.cpp` | `src/ui/register_table/register_container.cpp` | rename | Atlas | kamaldev, ui/fixes, fix/cache, 1to4firstIm, refiningmodes | `01df771` | Done |
| 32 | `src/ui/register_table/registercontainer.h` | `src/ui/register_table/register_container.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `01df771` | Done |
| 33 | `src/ui/register_table/registercontainer.ui` | `src/ui/register_table/register_container.ui` | rename (+ui_*.h) | Kites | fix/cache, 1to4firstIm, refiningmodes | `01df771` | Done |
| 34 | `src/ui/register_table/registermodel.cpp` | `src/ui/register_table/register_model.cpp` | rename | Atlas | kamaldev, ui/fixes, fix/cache, 1to4firstIm, refiningmodes | `fc1b5b5` | Done |
| 35 | `src/ui/register_table/registermodel.h` | `src/ui/register_table/register_model.h` | rename | Kites | fix/cache, 1to4firstIm, refiningmodes | `fc1b5b5` | Done |

## Corrections

- The commit message of **`fde8670`** states that `ui_cacheconfigwidget.h` was
  gone. That was wrong when written: AUTOUIC does not delete a generated header
  when its `.ui` is renamed, so a stale copy was still present in the
  incremental build directories. The clean build at the end of Phase 4 confirms
  the header is genuinely gone and that no source file references it. The
  commit itself is correct; only that one line of its message is not. It was
  left unamended deliberately.

## Status

All 35 rows are **Done**. The **Commit** column gives the short hash that
performed each rename, recovered from the rename records of the commits in
`pre-reorg-v0..HEAD` rather than typed by hand. Rows 17-19 carry two hashes
because the main window moved directory and changed file name in separate
commits: `767f19f` moved `src/ui/mainwindow/` to `src/ui/main_window/`, and
`7f497ab` renamed the files within it.

As a cross-check, each row's recorded **New path** was compared against the
path git actually arrives at by following that file's rename chain from its
old path. All 35 agree.

## Kites-origin files that upstream feature branches have also changed

These 18 are the rows where rename and upstream divergence overlap. None is on
`upstream/main`; all are on unmerged feature branches. Listed so that whoever
merges one of those branches knows to expect a rename-vs-edit resolution.

- `src/ui/cache_tab/cacheconfigwidget.cpp` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/cache_tab/cacheconfigwidget.h` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/cache_tab/cachemodel.h` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/cache_tab/cachetab.h` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/common/kitestab.cpp` — 1to4firstIm, refiningmodes
- `src/ui/common/kitestab.h` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/compiler_tab/compilertab.h` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/mainwindow/mainwindow.ui` — 1to4firstIm, refiningmodes
- `src/ui/memory_tab/memorymodel.h` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/memory_tab/memorytab.h` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/memory_tab/memorytab.ui` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/processor_tab/processortab.h` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/processor_tab/processortab.ui` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/profiler_tab/profilertab.h` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/profiler_tab/profilertab.ui` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/register_table/registercontainer.h` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/register_table/registercontainer.ui` — fix/cache, 1to4firstIm, refiningmodes
- `src/ui/register_table/registermodel.h` — fix/cache, 1to4firstIm, refiningmodes

## Not renamed, on purpose

Everything else keeps its current path. In particular `src/processor`,
`src/ui/dialogs`, `src/assembler`, `cli/`, `examples/` and `external/` are
left alone: they are wholly or almost wholly Kites-origin, and `upstream/main`
is actively changing the assembler and processor layers.

## Unrelated removal

`100` — a zero-byte tracked file at the repository root with no references,
removed in its own commit before the renames began.
