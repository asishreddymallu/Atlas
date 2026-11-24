# Documentation index

Every file in `docs/`, with who it is for:
- **user**: running Atlas;
- **student**: doing the labs;
- **developer**: changing the code;
- **auditor**: checking what was done and how it was verified.

## Using Atlas

| File | For | What it is |
| --- | --- | --- |
| [CLI.md](CLI.md) | user | `atlas-cli`: batch runs and the REPL, `--vm` names, output files, exit codes. |
| [cache_lab.md](cache_lab.md) | student, user | The Cache Lab: the live cache-line view, the 11 lab setup presets, the measured results table, and the exported lab record (`atlas.lab-record/1`). |
| [scheduler.md](scheduler.md) | student, user | Suggest reordering: what it proposes, when it offers a change, the demo programs and their cycle counts. |
| [profiler_metrics.md](profiler_metrics.md) | student, user, developer | The Profiler's extra metrics and the Cache tab's misses by cause: each definition, its Measured/Modelled label, when it is n/a, and what it does not measure. |
| [CUSTOM_REPLACEMENT_POLICY.md](CUSTOM_REPLACEMENT_POLICY.md) | student, developer | Writing a cache replacement policy as a Lua script: the hooks, the cache state a script sees, and examples. |
| [THEMES.md](THEMES.md) | user, developer | The ten themes: how the six VS Code ports' colours were mapped, the readability rules the `ThemeCatalogue` tests enforce, and the ports' licences. |

## Developing Atlas

| File | For | What it is |
| --- | --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | developer | Where things are and why: the naming convention, the top level, every `src/` subtree, "how to find X", and the build and test targets. |
| [EXTENDING.md](EXTENDING.md) | developer | Recipes, each with the exact files to touch and the tests to add: an instruction, a processor model, a replacement policy, a Cache Lab preset, a theme, a ribbon action, a verification checker. |
| [TESTING.md](TESTING.md) | developer | Building and running the GoogleTest suite (serially), and the coverage build. |
| [coding_convention.md](coding_convention.md) | developer | Naming and formatting rules, and the warning not to reformat `.ui` files. |
| [FUTURE_WORK.md](FUTURE_WORK.md) | developer | The open entries of `audit/known_issues.md`, grouped by area, and what the original scope did not deliver. |
| [images/](images/) | user | The screenshots the top-level README shows. |

## Design records

| File | For | What it is |
| --- | --- | --- |
| [design/editor_shortcuts.md](design/editor_shortcuts.md) | developer | Every key the editor's Input pane handles, and the test that pins it. |
| [design/editor_ui_exploration.md](design/editor_ui_exploration.md) | developer, auditor | A historical survey of the editor and diagnostics architecture, from 2026-11-02. |
| [design/repo_layout_plan.md](design/repo_layout_plan.md) | auditor | The historical survey made before the repository was reorganised. |

## Audit and history

| File | For | What it is |
| --- | --- | --- |
| [audit/known_issues.md](audit/known_issues.md) | developer, auditor | Every verified finding, with evidence, fixed or deliberately left open. The source for FUTURE_WORK.md. |
| [audit/BASELINE_AUDIT.md](audit/BASELINE_AUDIT.md) | auditor | The baseline audit of the fork (2026-11-02): build, tests, warnings. |
| [audit/baseline_warnings.txt](audit/baseline_warnings.txt) | auditor | The raw compiler and linker warnings that audit recorded. |
| [audit/disabled_tests.md](audit/disabled_tests.md) | auditor | Every disabled test: why it was disabled, what was re-enabled, and what remains. |
| [audit/path_mapping.md](audit/path_mapping.md) | auditor | Old to new paths for every file the repository reorganisation moved. |
| `CHANGES_FROM_KITES.md` | auditor | The changes since the fork, linked from `NOTICE.md`. Written by the maintainer, not generated. |

Outside `docs/`:
- the [top-level README](../README.md);
- [CONTRIBUTING.md](../CONTRIBUTING.md);
- [verification/README.md](../verification/README.md), the independent checkers;
- [verification/RESULTS.md](../verification/RESULTS.md), their last run;
- [NOTICE.md](../NOTICE.md) and [LICENSE.md](../LICENSE.md), the origin and the licence.
