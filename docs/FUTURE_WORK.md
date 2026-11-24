# Future work

This page lists every entry in [audit/known_issues.md](audit/known_issues.md) that is still open, grouped by area. That file is the only source: each line here is the entry's own summary, and the entry has the evidence, file and line references, and any reproduction. If the two disagree, the entry is right.

An entry counts as open when all three of these hold:
- its heading is not marked FIXED, WITHDRAWN, DONE or Resolved;
- its body does not record a later fix;
- a later entry has not fixed it.

Entries that record a deliberate trade-off rather than a defect are marked *(design limit)*.

## Assembler and instruction set

| # | Open item |
| --- | --- |
| 2 | `li` has a range check that is always true, so its error path is unreachable. |
| 4 | A syntax error underlines the mnemonic, not the operand at fault. |
| 5 | Six errors from the second resolution pass report no column. |
| 6 | Columns count bytes, not characters, so a non-ASCII character shifts every later column. |
| 8 | A custom pseudo-instruction whose expansion uses ", " is silently skipped. |
| 54 | `fmv.s.x` and `fmv.x.s` are in the name table but not in the syntax map, so the lexer rejects them. |
| 55 | The fence-instruction spelling in the tables is `fence_i`, not the standard `fence.i`. |
| 59 | A label named like a register cannot be referenced, and the in-memory path reports "Could not open the file". |
| 69 | `assemble(std::istream&)` reports every parse error as "Could not open the file". |
| 71 | A single-precision value is zero-extended in an f register, not NaN-boxed. |
| 77 | `li` of a large constant is a dependent chain that the no-hazard pipelines run without stalling. |
| 113 | The RMM rounding mode (round to nearest, ties to max magnitude) is not implemented. |
| 117 | `cycle`, `instret` and numeric CSR addresses do not assemble, and nothing counts into them. |
| 120 | `ebreak` assembles to `0x00001073`, a CSR instruction. What `ebreak` should do is undecided. |
| 141 | `fence`, `fence_i`, `fmv.s.x` and `fmv.x.s` are listed as instructions but do not assemble. |

## Pipelines and timing

| # | Open item |
| --- | --- |
| 80 | `stall_cycles_` is a countdown, not a total, but the UI calls it "StallCycles". |
| 119 | A CSR instruction is not a producer to the hazard unit (a question left open). |
| 122 | On nh-f, `jalr x5, 279(x5)` three instructions after `feq.s x5` jumps to the wrong place. Not investigated further. |
| 123 | An instruction served in writeback costs 4 bubbles, not 3 (a design question). |
| 143 | An ecall or CSR instruction costs four cycles on the pipelines, not the three the comments say. Only the comments are wrong. |
| 145 | The branch-misprediction tooltip says "two instructions" are flushed, but a taken branch costs three cycles. |
| 146 | The hazard unit stalls an instruction on the wrong path, and Stall cycles counts it (a design question). |
| 155 | The Stall cycles rows show "n/a" under a [Measured] tag on nh_f and nh_nf (cosmetic). |

## Processor state, Undo and threads

| # | Open item |
| --- | --- |
| 63 | The UI thread reads processor state owned by the worker thread during a run. |
| 86 | A pipelined `Run()` stops on the breakpoint `LoadProgram` plants past the last instruction. The window and, since final/tidy, the CLI clear it after a load, but the core still plants it. |
| 97 | RVSS Undo and Redo do not restore the control unit, so the single-cycle wires describe the wrong instruction. |
| 99 | Stopping a paused run makes the old processor clock once more, and that clock can arrive after a processor switch. The stage selection guards against it; the entry records no general fix. |
| 100 | The single-cycle stage cell shows the wrong instruction after Undo (same root cause as 97). |
| 112 | Undoing an ecall read does not give the input back, so stepping it again waits for input. |

## Caches

| # | Open item |
| --- | --- |
| 83 | The cache's own undo history is never called, so Undo doubles the hit, miss and eviction counters. |
| 130 | A no-write-allocate store miss reaches the next level as four byte writes, so L2 counts four misses (asked, not decided). |
| 131 | `CacheConfig::lineCount` holds the number of sets, and the UI's "2^n Lines" sets it. Only the label and field name are wrong. |
| 132 | The pipelines count wrong-path instruction fetches in the instruction cache (what hardware does; recorded). |
| 133 | There is no random replacement policy. |
| 160 | The hit rate is a fraction (0 to 1) in the engine. Any future display or export must convert it before adding "%". |

## Circuit diagram

| # | Open item |
| --- | --- |
| 94 | `H_NF_Processor.json` leaves 27 of its 46 wires unnamed, so much of what h-nf computes lights nothing. |
| 98 | Several "conditional" wires are also in the always-on backbone, so their conditions never show. |

## User interface and editor

| # | Open item |
| --- | --- |
| 1 | `ProfilerEditor::heatColor()` is dead code. |
| 12 | "Custom policy..." is not on the Cache ribbon page; it is reached only through the replacement-policy combo. |
| 24 | The Output (disassembly) pane has no Find. |
| 27 | The find-match highlight lowers the contrast of muted syntax colours, such as comments. |
| 33 | Two pre-existing costs and one paint bug outside the editor's own code, such as the autocompleter rebuilding the document on every key press. |
| 37 | There is no instruction documentation to show beside a completion suggestion. |
| 38 | The suggestion list's colours are derived from theme colours, not themed. |
| 40 | The per-block label copy is stale after a document swap (OPEN in its heading). |
| 41 | The label rescan after a structural edit costs 67 ms on a large file. |
| 46 | Find references (Shift+F12) and Rename (F2) take time linear in the number of references. |
| 48 | `contextAt()` keeps scanning past characters at which the lexer stops (left open as a judgement). |
| 50 | The label index and the lexer disagree about which token is the reference on one kind of malformed line. |
| 88 | Every theme fills a Disabled palette group that the stylesheet makes unreachable. |
| 151 | A long timing-grid jump takes 1.7 s; the 1 s target is not met. |
| 10 | Ribbon buttons are not in the keyboard tab chain *(design limit)*. |
| 11 | The window cannot be narrower than 849 px *(design limit)*. |
| 14 | Ribbon separators are below 3:1 contrast on purpose *(design limit)*. |
| 21 | A regex search may match across lines *(design limit)*. |
| 22 | A search stops at 10,000 matches *(design limit)*. |
| 23 | "Whole word" means `[A-Za-z0-9_]`, not Unicode word characters *(design limit)*. |
| 25 | A replacement that changes the number of lines moves breakpoints by a pinned rule *(design limit)*. |
| 28 | Only `( ) " '` are auto-closed *(design limit)*. |
| 29 | Bracket matching does not cross a line *(design limit)*. |
| 30 | Editor zoom is a session delta and is forgotten on restart *(design limit)*. |

## Build, tests and tooling

| # | Open item |
| --- | --- |
| 26 | A new `Q_OBJECT` header can end up with no moc. |
| 39 | Two of the suggestion list's rules cannot be tested in the test binary, which links no theme resources. |
| 49 | Syntax highlighting cannot be checked by rendering in the test binary, for the same reason. |
| 64 | `vm_state/` follows the working directory, so the app behaves differently depending on where it is launched. |
| 91 | A `MemoryTab` that is never shown has one row, and a test can mistake that for centring. |
| 92 | A rewritten `.ui` file can be skipped by the next incremental build. The workaround is a second build. |
| 118 | The out-of-tree livesweep and modelsweep gates cannot see flags or cycles. Those gates were not imported into `verification/`. |
| 134 | `examples/cache_test_simple.asm` never ends: it closes on a self-loop, kept in place by request. |
| 150 | The GUI tests flake under `ctest -j`, because they share `test/vm_state/temp.asm`. Run the suite serially. |

## Not listed, and why

| Reason | Entries |
| --- | --- |
| Fixed later and recorded in a later entry | 9 (by 68), 51 (by 109), 53 (by 111), 56 and 65 (the CLI crash, fixed with 87 on final/tidy), 3 (the label now reads "Size:"), 147 (the Profiler pane widened on final/polish, note 147.1) |
| Records, corrections or intended changes, not defects | 31, 32, 34, 35, 36, 42, 43, 44, 47, 67, 156 |
| Headed FIXED, WITHDRAWN, DONE or Resolved | all others |

## Delivered since the plan

- **Standalone packages (final/package).**
  - `scripts/package_linux.sh` writes an AppImage and a tar.gz, and `scripts/package_windows.ps1` writes a zip.
  - `scripts/check_package.sh` / `.ps1` are the smoke checks.
  - The workflow `.github/workflows/release.yml` builds and checks the packages, and publishes them to a GitHub Release for a version tag.

  What was verified:
  - CI run 45406566957 on `final/package` (commit 92534aa) built, packaged and smoke-checked the AppImage and the tar.gz on ubuntu-22.04, and the Windows zip on windows-latest.
  - By hand on Fedora 44, the Linux artifacts of that run passed `check_package.sh` (AppImage and tar.gz). The AppImage was opened, `examples/scheduler/load_use_original.s` ran on the 5-stage forwarding model in 11 cycles, and the Processor tab, the Profiler tab and Help > About were looked at.
  - The Windows zip was built and checked by CI only.

  Building the package on Fedora 44 itself needs a newer patchelf (see the comment in `scripts/package_linux.sh`). The CI package is not affected.

## Not delivered from the original scope

- **Packaging beyond the AppImage, tar.gz and zip:**
  - an installer (MSI, .deb or .rpm);
  - a macOS build;
  - code signing (the Windows executable is not signed);
  - a hand run of the Windows zip.
- **The rest of the Profiler metrics on the roadmap.** A first set was built on `final/metrics` ([profiler_metrics.md](profiler_metrics.md)): the instruction mix, branch statistics, speed-up over single-cycle, forwarding events, the hottest loops, and misses by cause. None of the six was skipped. Not built:
  - stage occupancy;
  - estimated run time;
  - stalls per source line;
  - dependence distance;
  - average memory access time;
  - register usage;
  - static versus dynamic instruction count;
  - critical path.
- **Hardware boards.** Running programs on, or comparing against, physical RISC-V boards was not done. Atlas is a simulator only.
