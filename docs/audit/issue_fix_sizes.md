# Fix sizes of the fixed known issues

Companion to `issue_fix_sizes.csv`. Both were generated on 2026-11-24 from `known_issues.md` and the history of `final/verification` (base 2a761ef). `known_issues.md` was not changed.

## Flag rule (applied mechanically)

- **SMALL**: at most 15 changed source lines (added + removed) in at most 2 source files.
- **LARGE**: more than 150 changed source lines, or more than 5 source files.
- **MEDIUM**: everything else.

Source = `src/`, `cli/`, `resources/`, and any `CMakeLists.txt` outside `test/`. Test lines (`test/`) are counted separately and do not affect the flag. Sizes are summed over all the linked commits of an entry (each commit diffed against its parent with `git show -M --numstat`). A commit linked to two entries (for example 72 and 116) is counted in full for each.

## Which entries

Every `## N.` entry whose heading says FIXED, Resolved, DONE or fixed: **60**. Entry 86 is added as a 61th: its heading is not marked, but its body records a partial fix on final/tidy (the CLI clears the planted breakpoint; the core is unchanged), and commit `9bb534d` names it. No heading says "partly fixed".

## How entries were linked to commits

The history refers to entries in these ways:
1. **Commit subject names the entry**: `... (known_issues 72, 116)` or `(known issues 87, 86)` or `(known issue 147)`. Used first. Commits that change only Markdown/`docs/` are dropped, because they record an entry rather than fix it.
2. **The entry's own text cites the fixing commit**: a hash on a line that says `Fixed in`, `In <hash>, ...` or `with the tests in`, and not on a cross-reference (`See **N**`). Used only when (1) gives nothing. Hashes the entry cites as the cause (`Introduced by`, `Was: ... in`) are not used.
3. Not used, because it would be a guess: docs commits that record a status (`docs: 84 FIXED (the chooser)`, `docs: 89 and 90 FIXED`) or a range with a task tag (`docs: ... known_issues 148-151 (T3 item 4)`), which never name the code commit.

Linked: **43** of 61. Unlinked: **18**.

| Linkage | Entries |
| --- | ---: |
| entry text cites the commit hash | 13 |
| commit subject names the entry | 30 |

## Counts per flag

| Flag | Entries |
| --- | ---: |
| SMALL | 9 |
| MEDIUM | 19 |
| LARGE | 15 |

## Baseline files

`all_non_test_files_in_baseline` is `yes` when every non-test, non-documentation file the fix changed already existed at the baseline tag v1.1.8 (`7ddb6f33aada55fc5e852f6a18df0faf85ccf4ba`): the file is not listed as Added by `git diff -M --diff-filter=A <baseline> <fix commit>`, so renamed files count as existing. Markdown and `docs/` files are left out, because `docs/audit/` did not exist at the baseline.

| All changed non-test files in baseline | Entries |
| --- | ---: |
| yes | 32 |
| no | 11 |

## Unlinked entries

| Number | Status | Title |
| ---: | --- | --- |
| 13 | Resolved | the dead `QToolBar` rules are gone |
| 16 | Resolved | a checkable menu entry with an icon showed no check mark |
| 17 | Resolved | the Cache tab's content sat a margin left of every other tab's |
| 18 | Resolved | breakpoints follow their line through an edit |
| 19 | Resolved | Debug Run read past the end of an empty program-counter vector |
| 82 | FIXED | Undo into the middle of a program, then continue, lost every register write |
| 84 | FIXED | the processor chooser matched its own rows by text, and preselected by enum order |
| 89 | FIXED | both data panels put their controls below the data they described |
| 90 | FIXED | the Double Word column had one byte order, no way to change it, and no label saying which |
| 93 | FIXED | nothing checked that a wire name a processor lights is a wire that exists |
| 148 | FIXED | Jumping forward on the timing grid after Run or Undo stopped dead |
| 149 | FIXED | A bare ProcessorManager threw on a checkout without test/vm_state/ |
| 152 | FIXED | The timing grid's explanation outlived its trace |
| 153 | FIXED | The Instruction Type table counted the program, not the run |
| 154 | FIXED | Forwarding producers and redirect "by" had instruction word 0 in the trace JSON |
| 157 | FIXED | The Cache tab showed the hit rate as a fraction with a percent sign |
| 158 | DONE | The Cache tab's Statistics box shows Evictions |
| 159 | FIXED | The Cache tab showed one write policy and sent another |

## CSV columns

`number, status, title, fix_commits, how_linked, non_test_files_changed` (files outside `test/`, excluding Markdown/`docs/`), `source_files_changed, source_lines_added, source_lines_removed, test_lines_added, all_non_test_files_in_baseline, flag`.
