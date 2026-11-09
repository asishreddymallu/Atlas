# Known Issues

Verified findings that are deliberately left unfixed, with the reason.

## Dead code

### `ProfilerEditor::heatColor()` has no callers

- **Where**: `src/ui/profiler_tab/profiler_editor.cpp:77-90`, declared in
  `profiler_editor.h:48`.
- **What**: returns one of four hard-coded colours (`#D85A30`, `#EF9F27`,
  `#FAC775`, `#FAEEDA`) from a `hitCount` on thresholds `0.80 / 0.50 / 0.20`.
- **Evidence**: the only heat colour actually painted comes from
  `heatBackgroundColor()`, called at `profiler_editor.cpp:56` and `:133`.
  `heatColor` is referenced nowhere outside its own definition and declaration.
- **Status**: left as-is. It was excluded from the Phase C token migration on
  purpose, so its four literals are the only hard-coded colours remaining in
  the profiler. Either wire it up to the gutter columns or delete it; until
  then it is unreachable and its colours never reach a screen.
- **Related**: `heatBackgroundColor()` also contains a duplicated early return
  (`profiler_editor.cpp:94-98` repeats the same `m_maxExecutionCount == 0 ||
  hitCount == 0` guard twice). Harmless, but it is the same function.
