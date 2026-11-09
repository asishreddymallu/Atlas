# Known Issues

Verified findings that are deliberately left unfixed, with the reason and the
evidence. Nothing here is a guess; each entry names the lines it was read from.

---

## 1. `ProfilerEditor::heatColor()` is dead code

- **Where**: `src/ui/profiler_tab/profiler_editor.cpp:77-90`, declared at
  `profiler_editor.h:48`.
- **What**: returns one of four hard-coded colours (`#D85A30`, `#EF9F27`,
  `#FAC775`, `#FAEEDA`) selected by thresholds `0.80 / 0.50 / 0.20`.
- **Evidence**: the only heat colour that reaches a screen comes from
  `heatBackgroundColor()`, called at `profiler_editor.cpp:56` and `:133`.
  `heatColor` is referenced nowhere outside its own definition and
  declaration.
- **Status**: deliberately excluded from the Phase C theme-token migration, so
  its four literals are the only hard-coded colours left in the profiler. They
  never render. Either wire the function up to the gutter columns or delete
  it.
- **Related**: `heatBackgroundColor()` repeats the same
  `m_maxExecutionCount == 0 || hitCount == 0` guard twice
  (`profiler_editor.cpp:94-98`). Harmless, same function.

---

## 2. Vacuous range check and unreachable error path in `li`

- **Where**: `src/assembler/parse_formats/pseudo_formats.cpp:212`, with the
  dead branch at `:243-250`.
- **What**:
  ```cpp
  if (imm <= UINT64_MAX)   // imm is uint64_t - always true
  ```
  The guard can never be false, so the `else` at `:243` - which records
  `"Immediate value out of range"` and an `ImmediateOutOfRangeError` quoting
  `-2^63 <= imm <= 2^63 - 1` - is unreachable.
- **Evidence**: the two comments immediately above the check
  (`pseudo_formats.cpp:210-211`) already state both facts: the original
  `0 <= imm` was tautological and tripped `-Wtype-limits`, and the else block
  is unreachable because `std::stoull` throws on out-of-range before control
  reaches here.
- **Consequence**: an out-of-range immediate on `li` surfaces as whatever
  `std::stoull` throws rather than as the tailored diagnostic, so the user
  does not get the "Expected: -2^63 <= imm <= 2^63 - 1" hint that the dead
  branch was written to give.
- **Status**: untouched. This is assembler logic, explicitly out of scope for
  the UI work. Fixing it properly means catching `std::out_of_range` around
  the `stoull` call and routing it to the existing error path - an assembler
  change that needs its own branch and test coverage.

---

## 3. "Size in Bits" label contradicted its own value

- **Where**: `src/ui/cache_tab/cacheconfigwidget.ui` (label `label_9`), value
  written at `cacheconfigwidget.cpp:127`.
- **What**: the label read `Size in Bits:` while the field is populated with
  ```cpp
  ui->sizeLineEdit->setText(QString::number(newStats.cacheSizeInBytes) + " bytes");
  ```
  i.e. a **byte** count with a `" bytes"` suffix.
- **Action taken**: the label was changed to `Size:` in the Phase C2 commit.
  That is correct whichever unit is really meant, because the value carries
  its own unit.
- **Still open**: which unit is actually correct. If `cacheSizeInBytes` really
  holds bits and is misnamed, the suffix at `cacheconfigwidget.cpp:127` is
  wrong and so is the field name. If it really holds bytes, the original
  label was simply wrong and this is now resolved. Determining which requires
  reading the cache model's size accounting - simulator logic, out of scope
  for the UI pass.
