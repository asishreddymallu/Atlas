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

- **Where**: `src/ui/cache_tab/cache_config_widget.ui` (label `label_9`), value
  written at `cache_config_widget.cpp:127`.
- **What**: the label read `Size in Bits:` while the field is populated with
  ```cpp
  ui->sizeLineEdit->setText(QString::number(newStats.cacheSizeInBytes) + " bytes");
  ```
  i.e. a **byte** count with a `" bytes"` suffix.
- **Action taken**: the label was changed to `Size:` in the Phase C2 commit.
  That is correct whichever unit is really meant, because the value carries
  its own unit.
- **Still open**: which unit is actually correct. If `cacheSizeInBytes` really
  holds bits and is misnamed, the suffix at `cache_config_widget.cpp:127` is
  wrong and so is the field name. If it really holds bytes, the original
  label was simply wrong and this is now resolved. Determining which requires
  reading the cache model's size accounting - simulator logic, out of scope
  for the UI pass.

---

## 4. Syntax errors underline the mnemonic, not the offending operand

- **Where**: `src/assembler/parser.cpp:497` (`Invalid syntax: Expected: ...`)
  and `:512` (`Unexpected token: ...`).
- **What**: both report `currentToken()`, which at that point is the
  instruction mnemonic. For `    addi x99, x0, 1` the span is columns 5..9
  (`addi`), not 10..13 (`x99`); for `    addi t0, t1` it is again 5..9 rather
  than the position where the third operand should have been.
- **Evidence**: the message itself is built as
  `getExpectedSyntaxes(currentToken().value)` and for that input returns
  addi's syntax, so `currentToken().value == "addi"`. The rich error beside it
  prints `:2:5:` with the caret under `addi`.
- **Status**: accepted. The span is defensible - "this instruction is
  malformed" - but it is not operand-level precision. The two tests covering
  it are named `...SpansTheMnemonicNotTheOffendingOperand` and
  `...SpansTheMnemonicNotTheGap` so they cannot be mistaken for a precision
  guarantee. Advancing to the failing operand means changing the parser's
  error-recovery position, not just the ParseError construction.

## 5. Six second-pass sites report no column

- **Where**: `src/assembler/parser.cpp` lines 691, 702, 723, 743, 755, 767.
- **What**: these run in the second resolution pass over `ICUnit` blocks,
  after the token stream is gone. `ICUnit` exposes only `getLineNumber()`, so
  they report `column = end_column = 0` (unknown) and consumers fall back to
  highlighting the whole line.
- **Category is correct**: they pass `DiagnosticCategory::Assembler`
  explicitly, with an explicit `0, 0` span, so an unknown branch label is
  categorised correctly even though it cannot be pointed at. Covered by
  `UnknownBranchLabelHasNoColumnButIsCategorisedAssembler`.
- **Why the column is not fixed**: giving them real columns needs a column on
  `ICUnit` and threading it through the code generator - a change to the code
  generator rather than to error reporting, and out of scope for this work
  package.

## 6. Columns count bytes, not characters

- **Where**: `src/assembler/lexer.cpp` - `column_number_` advances once per
  `std::string` byte.
- **What**: a UTF-8 multi-byte character costs as many columns as it has
  bytes. For `    .string "caf<e-acute>", "x"` the second literal starts at byte
  column 22, but a `QTextCursor`, which counts UTF-16 code units, puts the same
  position at column 21.
- **Scope**: non-ASCII can only appear in a string literal or a comment, so a
  shifted column requires non-ASCII *earlier on the same line* as the error.
  Rare, but real.
- **Status**: the model deliberately stays in byte columns end to end. The
  conversion to cursor offsets belongs at the UI boundary (stage 5), which
  must map the leading bytes of the line to UTF-16 units and clamp to the
  block length. Covered by
  `LexerTokenSpanNonAscii.ColumnsCountBytesNotCharacters`.

## 7. Two lexer column-accounting bugs, fixed

Recorded because they changed reported columns on directive lines.

- **`Lexer::directive()`** consumed the leading `.` with `++pos_` but never
  advanced `column_number_`, so the counter ran one behind for the rest of the
  line: the directive's own span was one short and every token after it on the
  same line reported a column one too small (`    .word 1, 2, 3` gave operands
  at 10/13/16 instead of 11/14/17). Fixed in `cabe69c`.
- **`Lexer::stringLiteral()`** read its start column after consuming the
  opening quote, so it pointed at the first character inside the literal. That
  looked correct only because the directive desync cancelled it out. Fixed in
  the same commit; the two changes are net-zero for a string on a directive
  line.
- Neither affects the 44 example programs, which contain no directives and no
  string literals. Verified against a dedicated 8-program directive corpus:
  machine code identical, failing set unchanged, and every token diff a
  column-only change on a directive line.

---

## 8. A custom pseudo-instruction whose expansion uses ", " is silently skipped

- **Where**: `src/custom_pseudo_manager/custom_pseudo_manager.cpp`, the
  expansion loop in `expandPseudoInstruction` (the `badExpansion` guard).
- **What**: each expansion line is split on whitespace, then every piece is
  split on `,` with `Qt::KeepEmptyParts`. An operand written as `sp,` followed
  by a space therefore yields `["sp", ""]`; the empty piece sets
  `badExpansion`, and the whole expansion is abandoned. The source line is
  emitted unchanged instead, with no warning anywhere.
- **Effect**: the pseudo-instruction is left in the text, the assembler then
  reports it as an unknown mnemonic, and the real cause - a formatting detail
  in the saved JSON - is invisible to the user.
- **Reproduced with**:
  ```json
  { "push": { "args": ["rs"],
              "expansion": ["addi sp, sp, -8", "sd rs, 0(sp)"] } }
  ```
  `push t0` is left verbatim and fails to assemble. Writing the same expansion
  without spaces after the commas works:
  ```json
  { "push": { "args": ["rs"],
              "expansion": ["addi sp,sp,-8", "sd rs,0(sp)"] } }
  ```
  which expands to `addi sp, sp, -8` / `sd t0, 0(sp)` as intended - note that
  the generator re-emits operands joined with `", "`, the very spacing it
  refuses to accept as input.
- **Status**: not fixed. The guard is meant to reject genuinely malformed
  templates such as consecutive commas; it should trim pieces before testing
  for emptiness rather than treating a trailing comma as malformed.

---

## 9. `TEST_DIR` is never set, so a dead glob scans the whole filesystem

- **Where**: `CMakeLists.txt:42`.
  ```cmake
  file(GLOB_RECURSE TEST_FILES "${TEST_DIR}/*.cpp")
  ```
- **What**: `TEST_DIR` is defined nowhere. `git grep TEST_DIR` over the tracked
  CMake files returns exactly one hit - this use site. The pattern therefore
  expands to `/*.cpp`, and `GLOB_RECURSE` walks the filesystem from root.
- **Observed**: evaluating that one line on its own with `cmake -P` reports
  `TEST_DIR is ''`, the pattern as `/*.cpp`, and a result list of **1015**
  files on this machine - the first being
  `/home/<user>/.cache/uv/archive-v0/.../generate_umath_validation_data.cpp`,
  i.e. outside the repository entirely. The same script takes **3.64 s**
  against **0.01 s** for a no-op script, so the cost is the directory walk.
- **Consequence is cost, not correctness**: the `TEST_FILES` set here is never
  read. `test/CMakeLists.txt:7` defines its own `TEST_FILES` with
  `file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS "*.cpp")` in its own scope,
  and that is the variable consumed at `test/CMakeLists.txt:10` and `:46`. The
  top-level value is shadowed before it could be used.
- **Status**: not fixed. Deleting line 42, or setting `TEST_DIR`, is a build
  change and this work package is documentation only. Noted so the configure-
  time cost is not mistaken for a CMake or filesystem problem.

---

## 10. Ribbon buttons are not in the keyboard tab chain

- **Where**: `src/ui/ribbon/ribbon_group.cpp` (`makeButton()` sets
  `Qt::NoFocus`), `src/ui/ribbon/ribbon_bar.cpp` (the `QTabBar` is
  `Qt::TabFocus`).
- **What**: the tab strip is the chrome's single tab stop. Arrow keys move
  between tabs from there; `Tab` then moves into the content. None of the
  ribbon's ~25 buttons can be reached by `Tab`.
- **Why it is this way**: the alternative puts a couple of dozen chrome
  controls in front of the editor on every `Tab` press, which is worse for
  the keyboard user the ribbon would otherwise be serving.
- **One exception**: the overflow chevron is `Qt::TabFocus`. It is the only
  keyboard route to a command whose group has moved into the menu, and a
  `QMenu` navigates by keyboard once it is open, so leaving it out would
  have made those commands unreachable rather than merely inconvenient.
- **Consequence**: a keyboard-only user reaches commands through the shortcuts
  (`F5` Run, `F9` Debug Run, `F6` Pause, `F10` Step, `Ctrl+O`, `Ctrl+S`,
  `Ctrl+Shift+M`), through the menu bar, which still carries File, Settings,
  Help and View, and - when the window is narrow enough for a group to
  overflow - through the chevron. **Commands that exist only on the ribbon
  and whose group is NOT overflowed have none of those three.** Those are:
  the Memory display base and Search, the register display base, the cache
  level, Compile and Copy to Clipboard, and the Editor/Expanded view toggle.
  Each of them mirrors a widget that is still inside its tab and still
  reachable by `Tab`, so none is unreachable - but the ribbon itself is not
  a keyboard route to any of them at a comfortable window width.
- **Status**: accepted design, recorded rather than fixed. A proper fix is
  ribbon key tips (Office's Alt-sequence) or making each group a single tab
  stop with arrow navigation inside it; neither is in the current scope.

---

## 11. The window cannot be narrower than 849 px

- **Where**: `src/ui/main_window/main_window.cpp` - the content splitter,
  holding the tab stack and `RegisterContainer`.
- **Measured** (1x, Fusion, `kites-dark`, 14 px base font): the content
  splitter's tab stack plus the register panel plus the handle come to
  **849 px**, which is the whole window's `minimumSizeHint()`. It was 799 px
  at the 13 px font; the text size moved it, not the ribbon.

  (A freshly shown window reports **796 px**, not 849, until the other tabs
  have been displayed once: a `QStackedWidget` takes the largest of its
  children's minimums, and a page that has never been laid out does not yet
  contribute one. Walk the six tabs and it settles at 849 and stays there.
  Anything probing this number has to walk the tabs first or it will read the
  transient.)
- **The ribbon is no longer part of this.** It used to force 1623 px, because
  a `QStackedWidget` takes the largest of its children's minimums and the
  busiest page therefore set the floor for every tab. `RibbonPage` now
  reports a zero horizontal minimum and `RibbonBar` reports
  `ribbonMinimumWidth()` - the persistent groups compact, plus the chevron.
  Measured at the shipped state that is **636 px**: Run 389 + File 210 +
  chevron 37. It does not bind, and would have to grow by 213 px to.

  (It read 543 until Stage 8, when `currentWidth()` was corrected to ask for
  `max(sizeHint, minimumWidth)` per leaf instead of the hint alone. The large
  buttons carry an explicit minimum width that compact mode does not shrink,
  so the old number understated every compact group by that floor - which is
  what let the panel squeeze the segmented runs.)

  Giving Problems, Search, Copy to Clipboard and Wires Stay Active an icon
  made their groups 24 px WIDER in full mode and much narrower in compact,
  because a button with an icon can drop its label where one without it has
  to keep it: File 263 -> 168, Find 115 -> 58, Datapath 181 -> 70, Output
  188 -> 59. The net effect on the floor was 594 -> 543.

  **The chevron DOES appear, on two of the six tabs.** An earlier draft of
  this entry said compact mode alone fitted at every reachable width and the
  chevron therefore never appeared. That was true of the widths the layout
  function believed in before Stage 8; correcting `currentWidth()` to ask for
  `max(sizeHint, minimumWidth)` raised the persistent area's compact total
  from 506 to 599 px, and 93 px is the difference between fitting and not.

  Measured at the shipped state (1x, Fusion, all four themes, every page, at
  1700 / 1366 / 1230 / 1200 / 1100 / 1000 / 900 / 849 px):

  | width | tabs with the chevron | what goes in the menu |
  |---|---|---|
  | 1700, 1366, 1230, 1200, 1100, 1000 | none | - |
  | 900 | Processor | Registers |
  | 849 | Memory, Processor | Display / Registers |

  Compacting alone still covers Editor, Cache, Compiler and Profiler at every
  width. Memory and Processor are the two busiest pages: Processor's compact
  total is 392 px and Memory's 284, against 599 for the persistent groups, so
  at 900 px and below the page runs out of room first.

  Three rows out of 48 - and the overflowed group is a segmented run of
  display bases both times, which is the one kind of group that cannot shed
  a label to get smaller, having no icons.

  Every action remains reachable by exactly one route at every width: a
  visible button XOR a menu entry, checked at all 54 width-page combinations
  with zero failures. The checked entry in the menu carries a check mark (the
  `QMenu::item:checked` rule, issue #16).

  Covered by the synthetic 0..2000 px sweep in `test/ribbon_layout_test.cpp`,
  by the widget tests that widen a group until it has to overflow, and - for
  the real widths above, which `kites_test` cannot reproduce because it links
  no stylesheet and no icons and so measures every group far narrower than
  the application does - by the out-of-tree `widthsweep` harness.

  (An earlier draft of this entry said 617. That was taken halfway through
  Stage 5, after the buttons lost their padding but before the captions
  shrank, and the captions are part of a group's width. 594 is the number
  `RibbonBar::minimumSizeHint()` returns now, and it is read from
  `RibbonGroup::fullWidth()` / `compactWidth()` - not from
  `minimumSizeHint()`, which is a different and larger number that the
  harness used to print beside it. 594 was itself superseded: 543 after the
  four icons landed, then 636 once Stage 8 corrected the measurement. 636 is
  what `RibbonBar::minimumSizeHint()` returns at the shipped state.)
  See `src/ui/ribbon/ribbon_layout.h`.
- **Below 849 px** the window simply refuses to shrink. Nothing is clipped
  and nothing is hidden; the layout stops rather than cutting anything off.
- **To go lower**, the register panel (minimum 225 px, maximum 350 px) would
  need a smaller minimum or a way to collapse, and the tab stack's 573 px
  would need looking at. Both are content changes, not ribbon changes, and
  neither is in scope here.
- **If a larger font** pushes the persistent groups plus the chevron past the
  content's own minimum, the floor rises to that instead. That is deliberate: the ribbon
  reports a larger minimum rather than clipping the one group that must
  always be reachable.

---

## 12. "Custom policy..." is not on the Cache ribbon page

- **Where**: `src/ui/cache_tab/cache_config_widget.cpp` -
  `onCustomPolicyClicked()` and the `repPolComboBox` handler above it.
- **What**: the handler is only ever reached from the replacement-policy combo
  having *already* moved to `Custom`, and it depends on that. Called while
  another policy is selected it leaves the widget and the cache disagreeing:
  on success the script loads into the cache while the combo still reads the
  old policy, and the next unrelated config edit emits `configChangedSignal`
  and reconfigures the cache straight back; on cancel it forces the combo to
  LRU with the signal blocked and emits nothing, so the widget claims LRU
  while the cache keeps running what was selected before; and the load-failure
  path reverts to `m_lastSelectedPolicy`, which is only written when a
  non-`Custom` policy is chosen through the combo.
- **Status**: deliberately not wired to a ribbon button. The in-tab combo is
  unchanged and remains the only route. A safe wrapper would replay the combo
  path rather than the dialog - move `repPolComboBox` to `Custom` unblocked
  and let the existing handler run - but two things need deciding first: what
  should happen when `Custom` is already selected (`setCurrentIndex()` would
  be a no-op), and whether the combo's index may be treated as the
  `ReplacementPolicy` value, since the widget writes by index
  (`setCurrentIndex(static_cast<int>(policy))`) and reads by data
  (`currentData().value<ReplacementPolicy>()`).

---

## 13. Resolved: the dead `QToolBar` rules are gone

- **Was**: the main toolbar was retired in Stage 4A but its `QToolBar`,
  `QToolBar::separator` and `QToolBar QToolButton` rules stayed in
  `resources/themes/template.qss`, matching nothing.
- **Now**: removed in Stage 5 - 68 lines. `git grep QToolBar` over `src/`
  confirms nothing constructs one; the only mention left is an unused
  `m_toolbar` pointer declared in `KitesTab`, which predates this work and is
  left alone.
- **The sidebar rules were NOT dead** and are kept. The `QListWidget` block
  was written for the navigation sidebar, but the Settings dialog's
  `settingList` uses every rule in it. Only the comment heading changed, so
  it no longer names a widget that does not exist.
- The ribbon needed its own `:checked` rule because the toolbar-scoped one
  never reached it; that rule, and the rest of the ribbon's styling, is
  under `#ribbonPanel` in the same file.

---

## 14. Ribbon separators are deliberately below 3:1

- **Where**: `resources/themes/template.qss` - `QFrame#ribbonGroupDivider`,
  `QFrame#ribbonInnerSeparator`, and the outline on
  `QToolButton[ribbonSegment]`.
- **Measured** against the panel ground: 1.32 (kites_dark), 1.31
  (kites_light), 1.11 (tokyo_night), 1.13 (tokyo_night_storm).
- **Why this is not a failure**: none of them is the only indicator of
  anything. A group is identified by its caption and its spacing as well as
  its rule; a segmented run is identified by its zero-gap adjacency and
  rounded ends as well as its outline; and which segment is *active* is
  carried by the accent border and selection fill, which measure 5.43 to 6.79
  against the panel and clear 3:1 everywhere.
- **Raising them would undo the fix.** These lines were `QFrame::VLine` with
  a Plain shadow, taking a near-white colour from the palette at roughly
  10:1, and that is exactly the harshness Stage 5 was asked to remove.

---

## 15. Resolved: ribbon page groups were laid out but not painted

- **Symptom**: on every tab except Editor, the groups belonging to that tab's
  ribbon page drew nothing. They were constructed, laid out, at the right
  geometry, `isVisible()` true and `isHidden()` false - and blank.
- **Cause**: `RibbonGroup::measure()` wrapped its mode flip in
  `setUpdatesEnabled(false)` and `setUpdatesEnabled(true)`, restoring
  whatever `updatesEnabled()` returned on entry. That flag is **inherited**:
  disabling updates on a widget sets `WA_UpdatesDisabled` on its descendants,
  and `updatesEnabled()` reports the inherited value. A `QStackedWidget`
  switches updates off while it swaps pages, so a group measured during a
  page change read `false` as if it were its own and wrote `false` back onto
  itself. The stack then re-enabled painting for everything it had disabled -
  but not for the flag `measure()` had just set. The persistent Run and File
  groups live outside the stack and were never affected, which is why only
  the per-tab pages went blank.
- **Introduced by** `d7dbebf` (*feat(ribbon): measure a group in both modes,
  and say if it can overflow*), the commit that added `measure()`.
  `git log -S setUpdatesEnabled -- src/ui/ribbon/ribbon_group.cpp` names it
  and `6c56f71`, and nothing else.
- **Fixed in** `6c56f71` (*style(ribbon): classic buttons*), not in a commit
  of its own: the toggle was removed when the bug was found while reviewing
  that stage's screenshots. `measure()` no longer touches the flag at all.
  The flip is synchronous and Qt coalesces the repaints it raises, so there
  was never an intermediate state to suppress.
- **The tip of Stage 4B2 was affected.** Measured, not remembered: an
  out-of-tree probe built against `fb58737` shows a real, shown window and
  visits every tab, and reports 25 groups with painting switched off; the
  same probe against the current tip reports none. `QWidget::grab()` honours
  the flag, so the screenshots taken at the end of B2 show the page area
  blank as well - the evidence was already there and was not read. The
  screenshots were reported as "nothing is clipped at either size", which was
  true and beside the point.
- **A CORRECTION, and the commit is not amended.** `6c56f71`'s message says
  `measure()` "re-entered itself". That is wrong, and so is the sentence in
  the same message beginning "Changing a button style asks the parent chain
  for a new size, which asks the group for its widths again, so measure()
  re-entered itself". It does not re-enter: `setToolButtonStyle()` raises no
  synchronous event, and a probe calling `fullWidth()` from the buttons'
  event filters recorded zero re-entries. The mechanism is the inherited
  `updatesEnabled()` described above. The commit stands as written because
  rewriting history to tidy a diagnosis is worse than recording the
  correction here; `a646bf8`'s message carries the same correction, and this
  entry is the one to believe.
- **Guarded by** four tests in `test/ribbon_navigation_test.cpp`
  (`RibbonStage6.*`): every group still paints after construction, after
  visiting every page, at every width from 800 to 1700, and after being
  measured repeatedly and while an ancestor has painting switched off. Two of
  the four fail against the pre-fix `measure()`; the other two do not
  discriminate and are kept for coverage.
- **And by the ink check**, which is what should have caught it in the first
  place: an out-of-tree harness that grabs the ribbon panel and requires
  every button to put ink in its own rectangle. Against `fb58737` it reports
  188 blank button renderings across all 8 page groups and exits 1; against
  the current tip, none. See the row in `docs/ARCHITECTURE.md`.

---

## 16. Resolved: a checkable menu entry with an icon showed no check mark

- **Was**: Qt draws an icon where a menu check mark goes, so once Wires Stay
  Active gained `Icon::Wires` its menu entry was **pixel-identical checked and
  unchecked - 0 of 6929**.
- **Now**: three rules on `QMenu::item` in `resources/themes/template.qss`
  give a checked entry a 2 px `{{accent}}` bar down its leading edge and a
  fill. Entries without an icon keep their check mark as well. Measured,
  checked against unchecked, in all four themes:

  | menu | differing pixels |
  |---|---|
  | Settings > Processor (Wires Stay Active) | 4809–4890 of 6929 |
  | View (Problems) | 6185–6295 of 8815 |
  | Settings > Preferences (theme radio set) | 4958–5059 of 23450 |
  | File, Help, Settings (no checkable entries) | 0 |

- **Nothing else about the menus changed**: the 2 px the border takes is given
  back out of `padding-left`, and every menu captured before and after in its
  unchecked state differs by 0 pixels (Preferences by 21, which is the active
  theme's radio mark that the "after" capture forces off).
- The hovered bar is `{{foreground}}`, not `{{accent}}`: accent on selection
  is the same hue in `kites_dark` and falls to 2.53:1, so the indicator would
  have vanished under the pointer. Worst case across the four themes is now
  4.81:1 hovered, 5.44:1 at rest against the menu, 4.86:1 disabled.

---

## 17. Resolved: the Cache tab's content sat a margin left of every other tab's

- **Was**: zeroing the host layout's left margin in `8e30284` put the inner
  tab bar flush with the ribbon strip, and took the pane with it. The Cache
  pane's content ended up one layout margin left of the other five tabs'.
- **Now**: the same amount is given back inside each of the three pages, so
  the bar stays flush and the pane's content returns to where it was. The
  amount is read from the host layout rather than written down, and
  `src/ui/cache_tab/cache_tab.cpp` is the only file touched - the `.ui` is
  not.
- **Measured** on the Cache tab's content region, 700x200 px of
  `kites-dark`, against the capture taken before any of this:

  | state | differing pixels |
  |---|---|
  | before, host margin present | reference |
  | host margin zeroed | **13359** of 140000 |
  | inset given back | **9** of 140000 |

  The nine are a 9x1 strip at the top-left: the pane's hairline, which now
  reaches to the tab's left edge because the bar does. Nothing else moved,
  and nothing moved vertically.
- **Guarded by** `RibbonStage8.TheCachePaneKeepsTheOtherTabsLeftEdge`, which
  derives the inset from `QStyle::PM_LayoutLeftMargin` and cross-checks it
  against a tab that still carries it, so the derivation is shown to be the
  right number rather than assumed. It fails against the zeroed behaviour.

---

## 18. Resolved: breakpoints follow their line through an edit

- **Where**: `src/ui/editor_tab/main_editor.cpp` — the anchors and the
  `contentsChange` handler; `src/ui/editor_tab/editor_commands.h` —
  `EditorCommands::LineMap`.
- **Was**: a breakpoint was a 1-based line number in a plain
  `std::vector<uint64_t>` and nothing adjusted it. Any edit that inserted or
  removed a line left it pointing at different text. Pressing Enter at the
  top of the file already did it, and had since breakpoints existed; the
  line commands were the same arithmetic.
- **Now**: each breakpoint carries a `QTextCursor` **selecting its whole
  line and the newline after it**. Qt maintains the selection through every
  ordinary edit, so the line is read back from the anchor — and the
  selection going *empty* is the signal that the line itself was deleted,
  which a bare cursor cannot tell from a line that merely moved. Where the
  selection goes empty but the block count did not change, only the line's
  text was deleted, so the breakpoint stays.
- **Why not `QTextBlockUserData`**: measured, it does not even survive an
  Enter at column 0 — the data stays with the block that keeps the position
  while the text moves to a new one.
- **The editor's own commands do not use the anchors.** They apply as one
  `insertText` over a range and anchors collapse inside one: replacing two
  lines with the same two lines swapped left both anchors on the same line.
  Each command therefore returns a `LineMap` — for every line it rewrites,
  where that line went or that it is gone — and `Editor::applyEdit()`
  remaps through it inside the same edit block.
- **The semantics**, all covered by `test/editor_breakpoints_test.cpp`:

  | edit | breakpoints |
  |---|---|
  | typing inside a line | unchanged |
  | Enter at column 0 | the line moves down with it |
  | Enter mid-line or at the end | stays on the first part |
  | Backspace/Delete joining two lines | the joined line keeps exactly one |
  | deleting a multi-line selection | wholly removed lines lose theirs; a partly kept line keeps its own; the rest move up |
  | pasting multi-line text | lines below move down; breakpoints stay with their text |
  | Alt+Up / Alt+Down | the block and the line it jumped over swap, each carrying its own |
  | Shift+Alt+Up / Shift+Alt+Down | the original keeps it, the copy gets none |
  | Ctrl+Shift+K | removed with the line; the rest move up |
  | Ctrl+Enter / Ctrl+Shift+Enter | lines shift; insert-above carries the current line down |
  | Ctrl+X on a whole line | removed with the line |
  | Ctrl+V of a line copy | lines below move down |
  | Ctrl+/, Tab, Shift+Tab, Ctrl+], Ctrl+[ | unchanged |

  And the invariants, asserted after each of 200 random keys from a fixed
  seed: never below line 1, never past the end, never duplicated, and the
  count never grows.
- **Loading a file is unchanged.** `setPlainText` has always left the
  numbers exactly as they were, out of range or not, and still does; only
  the anchors are rebuilt. Telling that apart from a user wiping the
  document took measuring — `setPlainText` arrives as *two* changes and the
  first is indistinguishable from a select-all-and-delete by position and
  length alone. The undo stack is the difference: `setPlainText` clears it,
  an edit fills it.

### Residual limitations

- **Undo does not bring back a breakpoint that was removed with its line.**
  Ctrl+Z restores the text, and every breakpoint still on the document moves
  back with it, but one deleted along with its line is gone. An undo-aware
  breakpoint store was explicitly out of scope. The tests assert only that
  undo and redo leave every breakpoint in range and do not crash.
- **A breakpoint that is already out of range stays out of range.** The only
  way to get one now is `setBreakpoints()`, or setting the document text
  directly — **File > Open no longer leaves them behind**: the open path
  clears them, because a breakpoint is a line number in the file that was
  open. The clear is in `EditorTab::loadFileContent()`, not in
  `Editor::setPlainText()`, so every other caller of `setPlainText` keeps
  the old behaviour. No *edit* ever places one outside the document.
  (`OpenClearsBreakpoints.*`)

  Two things about that work are worth keeping rather than rediscovering.
  `Editor::clearBreakpoints()` briefly carried an explicit gutter repaint,
  with a commit message claiming the gutter would not repaint otherwise.
  That was wrong: a negative control deleting it left every case passing,
  because both callers already cause the repaint - opening a file sets the
  document, and the gutter's own "Remove All" calls `update()` on itself.
  The line was removed. And
  `OpenClearsBreakpoints.CancellingTheDialogClearsNothing` **documents the
  contract and is not a guard** - `QFileDialog::getOpenFileName` is static
  and modal, so no test can drive the cancel, and no mutation of the
  implementation makes that case fail. Cancel rests on the clear sitting
  inside the handler's `!filename.isEmpty()` branch, plus a manual check.
- ~~The Expanded pane receives the Input pane's line numbers verbatim.~~
  **Fixed.** `EditorTab::breakpointsForExpandedView()` reads the existing
  expanded-line → original-line mapping the other way and puts each dot on
  the FIRST expanded line that came from its source line. A
  pseudo-instruction expanding to several lines shows one dot, on the first
  of them; a breakpoint on a source line that produces no expanded line at
  all — a comment, a blank — gets none, because there is nowhere to put it.
  The mapping is recomputed from the text being shown, not read from
  `m_expandedToOriginalLine`, which is set at assemble time and is stale as
  soon as the source is edited. (`ExpandedBreakpoints.*`)
- **Diagnostic squiggles were already right** and still are: they are
  `ExtraSelection`s holding a `QTextCursor`, so the document moves them. An
  error on line 2 followed by a move-down, a copy-down and a comment toggle
  carries the squiggle to line 3, then 4, then onto the commented line.
  `ProblemsDock::problemCount()` stays where the last assemble left it,
  because the dock's rows are re-derived only by a run.

---

## 19. Resolved: Debug Run read past the end of an empty program-counter vector

- **Where**: `src/processor/processor_manager.cpp` —
  `ProcessorManager::updateEditorHighlight()`.
- **Was**: triggering **Debug Run** aborted before the program went
  anywhere. `debugRunSlot` → `runSlot` → `run` → `RVSSProcessor::Run()`,
  which emits `processorClockedSignal(processor_state_)` once more after its
  loop. `processor_state_.programCounters` is filled only by
  `setProcessorState()`, called from `StepAndNotify()`, so on a Debug Run
  that emit is reached with the vector still default-constructed and empty;
  the RVSS branch then read `programCounters[0]`.
- **A correction.** The first version of this entry said that without
  libstdc++ assertions "the same code reads past the end silently". That is
  wrong. `data()` is null on a default-constructed vector, so it is
  undefined behaviour either way, and a crash either way. Measured on this
  toolchain:

  | | |
  |---|---|
  | assertions on (the default here — Fedora's libstdc++ enables them with no flag from this project; `CMAKE_BUILD_TYPE` is empty and there is no `-D` of any kind) | `Assertion '__n < this->size()' failed`, abort |
  | assertions explicitly off | **segmentation fault** |

- **Fix**: nothing clocked means nothing to point at, so
  `updateEditorHighlight()` returns early on an empty vector. That is the
  only change in `src/processor/`.
- **What Debug Run does now**, measured end to end on five instructions,
  one per line, with a breakpoint on line 4. The highlighted line is read
  from the painted pixels, independently of the program counter:

  | event | highlighted line | program counter | Pause command reads |
  |---|---|---|---|
  | before anything | none | 0 | Pause |
  | **Debug Run** | 1 | 0 | **Resume** |
  | Step | 2 | 4 | Resume |
  | Step | 3 | 8 | Resume |
  | Step | 4 | 12 | Resume |
  | Resume (from a fresh Debug Run) | 4 | **12** | **Resume** |

  It pauses *before* the first instruction, the highlight sits on the
  instruction about to execute, Step advances it one line at a time, and
  resuming stops at the breakpoint. Running past the end clears the
  highlight and leaves the program counter at 20.

  Resuming from an instruction you have already stepped onto does **not**
  stop there again — `last_breakpoint_pc_` is what prevents it, and that is
  the usual debugger behaviour, not a defect.
- **Pinned** by `ProcessorHighlight.*` in `test/processor_highlight_test.cpp`,
  which needs no window and no resources: `processorClockedSlot` is a public
  slot, so handing it a default-constructed `ProcessorState` is the same
  call. Two cases, because the guard must skip nothing else — an empty state
  produces no highlight signal, a populated one still does. Without the
  guard the case does not fail, it aborts the test binary.

---

## 21. A regex search may match across lines

- **Where**: `src/ui/editor_tab/find_engine.cpp` — `compile()` always sets
  `QRegularExpression::MultilineOption`.
- **What**: `^` and `$` are per line, and a pattern like `one\ntwo` matches
  across a line break. `.` still does not cross one, because
  `DotMatchesEverythingOption` is deliberately off.
- **Why it is listed**: a Replace All whose pattern spans lines can change
  the number of lines, which is issue 25. Pinned by
  `FindEngineRegex.CaretAndDollarArePerLineAndAMatchMaySpanLines`.

---

## 22. A search stops at 10000 matches

- **Where**: `FindEngine::matchCap` in
  `src/ui/editor_tab/find_engine.h`.
- **What**: `findAll` stops after 10000 matches and sets `truncated`. The
  status line then reads "1 of 10000 (first 10000)". Navigation and Replace
  All only ever see that first 10000.
- **Why**: a 20,000 line file with a pattern that matches every line would
  otherwise produce a selection list Qt has to lay out. The cap is a
  parameter, so the behaviour is testable without building a 10000-match
  document (`FindEngineCap.TenThousandMatchesAndThenTruncated`).
- **Consequence to know**: Replace All on a truncated search replaces the
  first 10000 only. Running it again picks up the next 10000.

---

## 23. "Whole word" means `[A-Za-z0-9_]`, not Unicode word characters

- **Where**: `FindEngine::isWordCharacter()` and `wrappedForWholeWord()`.
- **What**: the boundaries are explicit lookarounds over `[A-Za-z0-9_]`,
  not `\b`. `\b` is Unicode-aware, so with it an accented letter would
  count as part of a word and a search for a word next to one would behave
  differently from the same search next to an ASCII letter.
- **The same rule wraps a regex pattern**, non-capturing, so the user's own
  group numbers are untouched and "whole word" means one thing in both
  modes (`FindEngineWholeWord.BoundariesAreNonWordCharactersOnly`).
- **Consequence**: in a language whose identifiers use non-ASCII letters,
  whole-word would break words at those letters. RISC-V assembly does not,
  so this is the right trade here and the wrong one elsewhere.

---

## 24. The Output (disassembly) pane has no Find

- **Where**: the bar is created by `Editor`, in
  `src/ui/editor_tab/main_editor.cpp`.
- **What**: Find works in the **Input** pane and in the **Expanded** pane,
  because both are `Editor`. The Output pane is `DisassemblyEditor`, a
  different class, so Ctrl+F does nothing there.
- **Not done, deliberately**: sharing the bar would mean hoisting it into
  `KitesEditor`, which also carries `ProfilerEditor` and `CompilerEditor`
  into the feature. That is a wider change than this stage was scoped for,
  and it was not made silently.
- **Replace is refused in the Expanded pane** because it is read-only
  (`ReplaceReadOnly.AReadOnlyPaneRefusesToReplace`).

---

## 25. A replacement that changes the number of lines moves breakpoints

- **Where**: `Editor::replaceEveryMatch()` and `replaceCurrentMatch()` in
  `src/ui/editor_tab/main_editor.cpp`.
- **The rule, pinned rather than left undefined** —
  `ReplaceBreakpoints.TheRuleWhenAReplacementChangesTheNumberOfLines`:

  | replacement | breakpoint |
  |---|---|
  | same number of lines | stays exactly where it was |
  | splits a line into several | ends up on the **last** of the lines that come out of it |
  | removes lines | one on a line that is swallowed whole is dropped |
  | either | everything below shifts by the change in line count |

- **Why it is the anchors and not a `LineMap`**: Replace All is not an
  `EditorCommands` edit, so `applyEdit()`'s remap is not involved. The
  line-selection cursors from issue 18 do the work, and the rule above is
  what they produce — measured, then pinned.
- **Invariants hold regardless**: a sweep of 100 pattern/replacement pairs
  from a fixed seed, including replacements that insert and remove
  newlines, asserts no breakpoint leaves 1..lineCount, none duplicates, and
  the count never grows.
- **Not "fixed"**, because there is no obviously right answer for which of
  several new lines a breakpoint belongs to. It is written down instead.

---

## 26. A new `Q_OBJECT` header can end up with no moc

- **Where**: the build, not the code. `CMakeLists.txt` globs sources; Qt's
  AUTOMOC generates `moc_<name>.cpp` for headers carrying `Q_OBJECT`.
- **What**: adding a new header with `Q_OBJECT` can leave AUTOMOC listing it
  in `AutogenInfo.json` while generating no moc for it. The build then fails
  at link time with `undefined reference to Kites::X::staticMetaObject` and
  to each of the class's signals — an error that names the missing *symbol*,
  not the missing *file*, which is what makes it cost an hour.
- **The cure**: delete the build directory's autogen folder and reconfigure.

  ```
  rm -rf build/kites_core_autogen build/CMakeFiles/kites_core_autogen.dir
  cmake -S . -B build -G Ninja
  ```

  That is what `src/ui/editor_tab/find_bar.h` needed.
- **`CONFIGURE_DEPENDS` alone does not cure it**, and this was measured
  rather than assumed. With it on the globs, a throwaway header and source
  under the editor-tab directory carrying a `Q_OBJECT` class were picked up
  and **compiled** by a plain `cmake --build` with no reconfigure — ninja
  listed the new source as added — but **no moc was generated for the
  header**, by that build or by an explicit `cmake -S . -B build`
  afterwards, although AUTOMOC listed the header in its own info file. The
  build only passed because nothing outside that translation unit
  referenced the class, so the static library's object was never pulled in
  and the missing symbol never reached a link. The probe was then deleted.
- **So `CONFIGURE_DEPENDS` is still worth having** — a new `.cpp` or `.h`
  compiles without a reconfigure — it just does not help AUTOMOC.

---

## 27. The find match wash lowers the contrast of muted syntax colours

- **Where**: `Editor::refreshFindColors()` in
  `src/ui/editor_tab/main_editor.cpp` — the all-matches highlight is the
  editor background with 25% of the theme's `info` mixed in.
- **What**: ordinary foreground text on that wash stays at 5.32:1 or better
  in every theme, which is the gate the feature was held to. **Syntax
  colours that are deliberately muted do not.** Measured, contrast on the
  wash / on the plain page / and how visible the wash itself is:

  | wash | theme | Comment | Label | NumberLiteral | wash vs page |
  |---|---|---|---|---|---|
  | 0.15 | kites-dark | 3.46 / 4.59 | 6.16 / 8.17 | 6.80 / 9.01 | 1.33 |
  | | kites-light | 4.10 / 5.14 | 4.13 / 5.18 | 5.17 / 6.48 | 1.25 |
  | | tokyo-night | 3.78 / 5.20 | 7.85 / 10.80 | 6.43 / 8.84 | 1.38 |
  | | tokyo-night-storm | 3.45 / 4.86 | 6.63 / 9.34 | 5.43 / 7.65 | 1.41 |
  | 0.20 | kites-dark | 3.12 / 4.59 | 5.56 / 8.17 | 6.13 / 9.01 | 1.47 |
  | | kites-light | 3.78 / 5.14 | 3.81 / 5.18 | 4.77 / 6.48 | 1.36 |
  | | tokyo-night | 3.32 / 5.20 | 6.90 / 10.80 | 5.65 / 8.84 | 1.57 |
  | | tokyo-night-storm | 3.04 / 4.86 | 5.85 / 9.34 | 4.79 / 7.65 | 1.60 |
  | **0.25** (shipped) | kites-dark | **2.81** / 4.59 | 5.00 / 8.17 | 5.51 / 9.01 | 1.63 |
  | | kites-light | 3.49 / 5.14 | 3.52 / 5.18 | 4.40 / 6.48 | 1.47 |
  | | tokyo-night | **2.90** / 5.20 | 6.03 / 10.80 | 4.94 / 8.84 | 1.79 |
  | | tokyo-night-storm | **2.68** / 4.86 | 5.15 / 9.34 | 4.22 / 7.65 | 1.81 |

- **No amount fixes it.** `Comment` is below 4.5 on the wash at every amount
  tried, and it is already only 4.59 on the plain page in kites-dark — the
  wash cannot rescue a colour that starts near the line. Lowering the wash
  trades comment legibility for match visibility: at 0.15 the wash is only
  1.25–1.41 against the page, which is close to invisible.
- **Not changed.** Highlighting a match on a comment makes that comment
  harder to read for as long as the find bar is open. Written down rather
  than traded away silently.

---

## 28. Only `( ) " '` are auto-closed

- **Where**: `EditorCommands::isPairOpener()` in
  `src/ui/editor_tab/editor_commands.cpp`.
- **What**: `[` `]` and `{` `}` are not paired. No RISC-V addressing mode or
  directive uses them, so closing them would put characters on the screen
  that the assembler has no use for.
- **Consequence**: an editor used for something other than RISC-V assembly
  would want them. The list is one function.

---

## 29. Bracket matching does not cross a line

- **Where**: `EditorCommands::bracketAt()`.
- **What**: the partner is searched for only within the caret's own line. A
  `(` whose `)` is on the next line reports as unmatched and is drawn in the
  error colour.
- **Why**: a parenthesis in this language is an addressing mode — `4(sp)` —
  and cannot span lines. Searching the document on every cursor move would
  be work spent looking for something that cannot be there.
- **Consequence**: a multi-line expression in a future macro syntax would be
  reported as an error when it is not.

---

## 30. Zoom is a session delta and is forgotten on restart

- **Where**: `KitesEditor::setZoomDelta()` and `applyZoomedFont()` in
  `src/ui/common/kites_editor.cpp`.
- **What**: `Ctrl++` / `Ctrl+-` / `Ctrl+0` / `Ctrl+wheel` move a delta on
  top of the font configured in Settings, clamped to `[-6, +18]` around it.
  The setting is never written, so closing the window forgets the zoom.
- **Deliberate**: a zoom used to read one line should not quietly become the
  user's configured font size.
- **Applies to the Input, Expanded and Output panes.** `zoomEnabled()` is
  opt-in, so the profiler and compiler views are unchanged.
- **The floor is absolute, not six steps.** −6 steps from the default
  11 pt is 5 pt, which renders 7 px text on a 10 px line. −6 steps from a
  configured 8 pt would be 2 pt — a 3 px glyph on a 4 px line — so the
  clamp takes whichever is higher, the step limit or the delta that
  reaches `minRenderedSize` (5). Measured at configured 6, 8 and 11 pt:
  every one bottoms out at 5 pt / 7 px. The ceiling stays relative.
- **Measured range at the default 11 pt:** 5 pt (7 px, 9 px line) to
  29 pt (39 px, 51 px line). The line-number column fits its digits at
  every step of that range.
- The breakpoint dot **does** scale now — see the correction below.

---

## 31. The editor font size now comes from the per-widget stylesheet

- **Where**: `KitesEditor::applyEditorTheme()`.
- **What it fixes**: the application stylesheet carries
  `QWidget { font-size: … }`, and a stylesheet rule beats
  `QWidget::setFont()`. The editor therefore rendered at the **chrome's**
  font size whatever font `FontManager` handed it, which is why zoom had no
  visible effect until the size was written into the per-widget sheet the
  editor already had.
- **A side effect worth knowing, and not asked for**: the size configured
  in Settings now reaches the editor as well. Before this change the global
  rule overrode it, so changing the editor font size in Settings moved the
  family but not the size. This was found while making zoom work, not
  hunted for, and it is flagged rather than buried.
- **Also**: `applyEditorTheme()` must not read `font()` to find the size —
  with a stylesheet in play that returns the *resolved* font, so it would
  read back whatever it wrote last time. It computes from the configured
  font plus the zoom delta instead.

---

## 32. Corrections to 30 and 31, from measuring instead of looking

Three things recorded earlier in this file were wrong. They are corrected
here rather than quietly edited away, because the way each was found says
something about the checks.

- **"The breakpoint dot does not scale, and at −6 it is taller than the
  line it marks."** The first half was true, the second was not. It came
  from reading a screenshot, not a measurement. Measured at the default
  11 pt the −6 line is 10 px and the dot's ink is 8 px: it fitted. What a
  sweep of *configured* sizes then showed was a real version of the same
  bug — at a configured 8 pt the line is 4 px and the dot still 9, because
  the clamp was relative. The dot now shrinks to fit
  (`clamp(lineHeight - 2, 3, 9)`) and the floor is absolute, so it is
  inside its line at every delta and every configured size measured.
- **"Zoom's gutter work costs one character comparison."** That described
  the comparison and ignored building the string it compared in.
  `toPlainText()` ran on every cursor move and every key event. Measured
  on an 877,000 character document: 1804 µs per caret move, now 53 µs.
- **"The transcript is identical to the baseline."** It was compared
  against a file that had drifted. See 34.

---

## 33. Two costs and one bug that are outside the editor's own code

Found while measuring the above. All three are pre-existing and none is
touched, because they are outside the files this work is allowed to edit.

- **The autocompleter rebuilds the document on every keystroke.**
  `Editor::keyPressEvent` calls
  `LanguageService::completions(prefix, toPlainText())` for every key with
  a non-empty prefix. Measured at 877,043 characters it is **2.3–3.3 ms
  per key press**, which is all of the remaining typing latency: with it
  stubbed out a keystroke costs 83 µs and a caret move 53 µs. Typing `(`
  is already fast only because punctuation gives an empty prefix and skips
  it.
- **The Run button loses its shortcut hint.** `setActionHint()` appends
  `  (F5)` to the tooltip, but the path that resets the button when a
  program finishes calls `setToolTip("Run program")` directly and drops
  the suffix. Introduced by `871a869`. This is why the `17-final` row of
  the role transcript differs. In `src/ui/main_window/main_window.cpp`,
  which this work may not edit.
- **The find bar was see-through** — fixed, since it is in this work's
  files. `#findBar` carried `background-color` and a border from the day
  it was written and neither ever reached the screen, because `FindBar` is
  a plain `QWidget` and a plain `QWidget` has no `paintEvent` that asks
  the style to draw them. The same trap as `#ribbonPanel`'s separator
  line. It only became visible once a screenshot was taken at a width
  where the bar sits over code.

---

## 34. The role-transcript baseline was stale, and the gates said "identical"

The role-transcript baseline in the out-of-tree verification harness
(`baseline/role_transcript_f5a0a00`, a TSV beside the harness, not in this
repo) was recorded at `f5a0a00`. By the branch point `f76b7ed` — 70 commits later — **51 of its
220 rows no longer matched**: 20 `PROBLEMS` rows differing in the icon-tag
field, which was known, and **31 that were not**.

Every earlier report in this project that said the transcript was
"identical to the baseline" was comparing against that stale file, and the
md5 `f41286818d7ea889055807dfea9703aa` quoted for it was the **baseline
file's own**, not a current run's. The checks were not wrong about this
branch — they were not measuring what they claimed.

The 31 rows are one tooltip (see 33) and five snapshots of the ribbon in
its idle rather than its running form. None comes from the Debug Run fix
`a481e5a`, which only skips a highlight update when there are no program
counters.

**They are still unexplained.** An earlier version of this entry called
them environmental. That was a guess, and it was tested and found wrong —
see 36.

A new baseline is recorded at the branch point, with the commit in the
name, so the next comparison is against something that was true:
`role_transcript_f76b7ed`, md5
`b518ba12776391c34cc520e77585d013`. HEAD matches it exactly.

> **RETIRED.** That md5 is not a baseline, it is the signature of a
> BROKEN run. `transcript/main.cpp` opened `examples/ex_mem_fwd.s`
> relative to the working directory with no `is_open()` check, so a
> wrong CWD left the program empty, the debug session never started,
> every debug row came out "idle", and the transcript hashed to
> `b518ba12…`. Deleting that one file reproduces the value exactly,
> byte for byte, three times out of three.
>
> The gate is now an **A/B**: build the base commit and HEAD out of
> tree, run the same harness against both in one session with a correct
> CWD, and require 0 differing rows. No stored md5 is trusted, because a
> stored md5 cannot tell a clean run from a broken one. The harness now
> exits 3 with a message if it cannot open its program, so this
> particular silence cannot happen again.

---

## 35. Tests and harnesses shared the developer's real configuration

- **What it was**: `AppSettings` is `QSettings("Kites", "Kites")`, which on
  Linux is the Kites settings file under ~/.config. Nothing redirected it, so the test
  suite and every out-of-tree harness read and wrote the developer's own
  preferences. `FontManager::setSize()` and `ThemeManager::setTheme()` both
  write through immediately.
- **The keys at risk**: editor fontSize, editor fontFamily, theme id,
  theme editorId and compilerOutput borderStyle. `ThemeManager` also reads
  `QStandardPaths::AppDataLocation` for its themes directory.

### What it actually caused

1. **A harness sweep left the editor font-size key set to 3.** A probe that set a range of
   configured sizes did not restore the last one. The next `kites_test` run
   read 3 and `EditorZoom.ItClampsAtBothEnds` failed — correctly, because at
   3 pt the zoom floor really is 0. The test was reading a preferences file.
2. **Theme rewrites on every run.** The four-theme smoke harness ends on
   whichever theme it iterated to last and writes it. Measured against a
   sentinel: `tokyo-night` in, `kites-dark` out.
3. **A baseline that did not reproduce.** The stored role-transcript
   baseline differed from a fresh build of its *own commit* in 31 rows.

### What it is now

- `test/test_main.cpp` makes a per-process temporary directory before
  `QApplication`, seeds it (`fontSize=11`, the theme key set to tokyo-night), points
  `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_CACHE_HOME` at it and pins
  `QSettings::setPath()` to the same place. It exits 2 rather than falling
  back to the real file.
- Harnesses go through the hermetic wrapper script, which does the same for a child
  process and deletes the directory afterwards.

Measured against a sentinel in a fake home, never the real file: XDG alone
protects, `setPath` alone protects, with neither the sentinel comes back as
`fontSize=3`. Both are kept because they cover different things — XDG also
redirects `QStandardPaths`, `setPath` covers a platform that does not take
the native settings path from XDG.

`QStandardPaths::setTestModeEnabled()` was rejected: it appends a `qttest`
element, moving the path away from the seeded file, so the suite would start
empty rather than known.

**Still true**: the *application* writes these keys, as it should. This
work isolates the tests and harnesses, not the product.

---

## 36. The 31 transcript rows are not explained by configuration

Entry 34 guessed that the stale baseline came from configuration state.
Now that runs can be made hermetic, that guess was testable, so it was
tested. It is wrong.

`f5a0a00` was built from a `git archive` and its transcript run six times,
each in a fresh sandbox seeded with one key changed: theme tokyo-night,
kites-dark, tokyo-night-storm, kites-light; editor font size 9, 11, 14.

**All six produced the identical md5** `ad7d7c6cb3b3c42036f35296fac2d4aa`,
and every one differs from the stored baseline in the same 31
non-`PROBLEMS` rows. No configuration key moves a single one of them.

So what is known is only this:

- `f5a0a00` built and run on this machine today gives `ad7d7c6c…`, under
  every configuration tried.
- The baseline file stored for `f5a0a00` says `f41286818…`.
- Therefore the stored file was not produced by that commit on this
  machine, and configuration is not the difference.

What has **not** been ruled out: a different Qt or OS version, different
hardware timing in the five run-state snapshots, or a change to the
transcript harness itself since the baseline was taken. The harness lives
outside this repository and has no history to diff, which is a gap worth
closing separately.

Calling this "environmental" would be putting a name on an unknown. It is
recorded as unexplained, and the new baseline is taken hermetically so the
next comparison starts from something reproducible: three consecutive runs
of the current branch agree exactly.

---

## 37. There is no documentation to show beside a suggestion

- **Where**: `resources/instructions/instructions.json`, read by
  `CompletionProvider::loadMnemonicsAndDirectives()`.
- **What**: the file is a map of category to an array of **bare mnemonic
  strings** — 171 of them. No operand format, no description, no examples.
  `LanguageService::completions()` then merges those with the buffer's
  labels into one `QStringList`, so even the category is gone by the time
  the editor sees it.
- **Consequence**: two things in the VS Code port cannot be built.
  - The **details panel** (`Ctrl+Space` while the list is open, showing the
    selected item's documentation) has nothing to put in it. Not done.
  - The **detail column** on each row is drawn but is almost always empty.
    The column and its eliding rule exist so that data, if it ever arrives,
    has somewhere to go.
- **The kind badge** is derived from the symbol's shape instead: a leading
  `.` is a directive, a name the assembler knows as a register is a
  register, a name defined with `:` in the buffer is a label, anything else
  is an instruction. That is a guess from text, not a fact from the data.
- **Why not fix it here**: filling the JSON in is a content change to the
  instruction set, not an editor change, and `LanguageService` was left
  untouched on purpose — the injection seam for tests lives in the new
  editor code instead.

---

## 38. The suggestion list's colours are derived, not themed

- **Where**: `paletteNow()` in `src/ui/editor_tab/completion_popup.cpp`.
- **What**: `ThemeManager` exposes editor background, foreground, muted
  foreground, info, error, current line — but no border, surface, accent or
  selection getter. The list's frame, selected row, matched-character
  colour and dim detail are therefore **computed** from the colours that
  are exposed, rather than read from the tokens a designer would expect to
  set.
- **Consequence**: changing the `border` or `surface` token in a theme JSON
  does not move the suggestion list. It follows the editor's background and
  foreground instead.
- **Deliberate**: adding getters would have been the fourth file touched in
  a stage that was asked not to add theme tokens, and the derived colours
  are measured against the 4.5:1 gate on all four themes anyway.

---

## 39. Two of the suggestion list's rules cannot be tested in kites_test

Both were found only because the out-of-tree harness runs the real window.

- **`setTheme()` does nothing in `kites_test`.** There are no theme
  resources linked, so the four themes in
  `CompletionContrast.EveryColourClearsFourAndAHalfOnEveryTheme` all
  measure the **default** theme. It reported four identical numbers and
  passed while the accent on the selected row was 4.18:1 on kites-dark and
  4.19:1 on kites-light. The harness found it; the colour is now lifted.
- **There is no application stylesheet in `kites_test`.** So
  `CompletionGeometry.RowsFollowTheZoom` passed while, in the real window,
  `QWidget { font-size }` beat the popup's `setFont()` and the list stayed
  at 14 px while the code was 29 pt.
- **Consequence**: the in-tree contrast and zoom cases are guards against
  regression in the default theme only. The four-theme contrast table and
  the screenshots have to come from the harness, every time.

---

## 40. OPEN: the per-block label copy is stale after a document swap

- **Where**: the `contentsChange` handler and `documentLabels()` in
  `src/ui/editor_tab/main_editor.cpp`.
- **What is known.** `setPlainText` emits **two** `contentsChange` signals.
  Measured, for a 25-character document:

  ```
  contentsChange from=0 removed=25 added=0
  contentsChange from=0 removed=0  added=24
  ```

  The second carries a correct `charsAdded` spanning every block, so the
  handler rescans all of them and updates each block's stored label list.
  After both signals have run, that stored copy ought to be correct.

- **What is measured instead.** It is not. A build whose `documentLabels()`
  reads `stored->labels` rather than rescanning fails
  `CompletionLabelCache.DeletingTheColonRetiresTheLabel` **at its setup
  assertion** — the first query after a `setPlainText`, before any colon has
  been deleted — returning an empty label list for a document that defines
  one.

- **Cause: not pinned down.** It is not the first signal's stale write on
  its own, because the second signal's range covers block 0 and compares
  against what it holds. Beyond that, this has not been chased.

- **Consequence: none at present.** `documentLabels()` rescans every line
  when the dirty flag is set, so the answer the editor uses never depends
  on the stored copy. The stored copy's only job is change *detection* —
  deciding whether the flag should be set — and that works. The cost is one
  full scan per labels-actually-changed edit, not per keystroke.

- **How to reproduce it.** In `documentLabels()`, replace the rescan body

  ```cpp
  const QStringList found = scanLineLabels(block.text());
  auto *stored = static_cast<BlockLabels *>(block.userData());
  if (!stored) block.setUserData(new BlockLabels(found)); else stored->labels = found;
  labels << found;
  ```

  with one that trusts what is stored:

  ```cpp
  auto *stored = static_cast<BlockLabels *>(block.userData());
  if (!stored) { stored = new BlockLabels(scanLineLabels(block.text()));
                 block.setUserData(stored); }
  labels << stored->labels;
  ```

  then run `kites_test --gtest_filter='CompletionLabelCache*'`. One case
  fails, at its setup.

- **Why it is open rather than fixed.** The rescan is correct and cheap
  enough that chasing the stale copy would be work spent on a path nothing
  reads. It is written down so that anyone who later tries to make the
  stored copy authoritative — for a bigger document, say — knows there is
  something here to find first.

---

## 41. The label rescan after a structural edit costs 67 ms on a large file

- **Where**: `Editor::documentLabels()` in `src/ui/editor_tab/main_editor.cpp`.
- **Measured**, in the real window through the hermetic wrapper, with
  `processEvents()` between the edit and the query as real typing has:

  | document | after Enter | after a typed colon | first Ctrl-move onto a label |
  |---|---|---|---|
  | 877,043 characters | 66,885 µs | 52,054 µs | 54,889 µs |
  | 20,000 lines | 36,461 µs | 29,539 µs | 36,797 µs |

  The same query with the cache clean costs **1–2 µs**. So the whole figure
  is the rescan, and the first Ctrl-hover after an edit is simply the thing
  that happens to pay for it.

- **Where the time goes**: `documentLabels()` walks every block and calls
  `QTextBlock::text()`, which materialises a `QString` per block — 16,548
  of them on the large file. Making the tokeniser allocation-free (it
  returns token positions now, not copies) took it from 78 ms to 67 ms, so
  the token copies were real but minor; the per-block text is the rest.

- **Why it rescans at all**: because of **40**. Each block already caches
  its own labels, but that stored copy is measurably stale after a
  `setPlainText`, so `documentLabels()` recomputes everything rather than
  trust it. Fixing 40 is what removes this; micro-optimising the scan is
  not.

- **Consequence**: the first completion query or Ctrl-hover after Enter, or
  after typing a `:`, stalls for the length of a full rescan. Ordinary
  typing is unaffected — it does not dirty the cache — and the steady-state
  hover does not scale with document size.

- **What it costs at realistic sizes.** The two documents the entry was
  opened on are pathological; these are what a teaching file actually
  looks like. Measured through `hoverprobe` under the hermetic wrapper:

  | document | rescan after `Enter` | after a typed `:` | first Ctrl-move | steady hover |
  |---|---|---|---|---|
  | 200 lines | 529 µs | 496 µs | 613 µs | 8 µs/move |
  | 1,000 lines | 2,552 µs | 2,506 µs | 2,546 µs | 32 µs/move |
  | 5,000 lines | 13,596 µs | 12,640 µs | 13,620 µs | 155 µs/move |
  | 20,000 lines | 34,988 µs | 30,615 µs | 30,177 µs | 431 µs/move |
  | 877,043 chars | 71,300 µs | 71,304 µs | 75,661 µs | 602 µs/move |

  It is linear in the document. **At 200 lines it is 0.5 ms and nobody can
  feel it**; at 1,000 lines it is 2.5 ms, still under a frame. It becomes
  visible around 5,000 lines (13.6 ms, about one frame at 60 Hz) and is a
  clear stall at 20,000. Most Atlas programs are well under 200 lines, so
  this is a correctness-of-design problem rather than a hazard in daily
  use — which is why it is recorded rather than rushed.

- **Reproduction**: `atlas-verify/harness/hoverprobe`, run through the
  hermetic wrapper. It prints the dirty and clean query costs side by side.

---

## 42. A correction: the 82 ms first Ctrl-hover was the rescan, not the repaint

Commit `aac80a9` states that the 82 ms first-Ctrl-move-after-an-edit was
"drawing the underline: the first `setExtraSelections()` after an edit
forces the pending re-layout". **That is wrong**, and the commit message
cannot be amended, so it is corrected here.

The measurement that led to it compared three moves, one of which landed
on the left margin — a **non-label**. `updateLinkHighlight()` tests
`name.isEmpty() || !documentLabels().contains(name)`, and `||`
short-circuits, so that probe **never called `documentLabels()`**. It
measured a path with no rescan in it and I read the 793 µs as "the rescan
is cheap".

Timing `documentLabels()` directly (see **41**) gives 67 ms. The repaint
was never the problem.

The lesson is narrower than "measure more": the probe has to exercise the
branch being attributed. A short-circuit on the line above the thing you
are timing is enough to make the number meaningless.

---

## 43. `has.dot:` is no longer offered as a completion (intended, recorded)

The completion scanner used to decide "this line defines a label" with the
editor's own completion word, which accepts `.` and `$` because RISC-V has
`fadd.s`. The assembler's rule for a *label* is narrower: `[A-Za-z0-9_]+`,
with `.` explicitly rejected and `$` never in the scan at all.

Both now go through `LabelIndex::definitionsOnLine()`, the single
definition of the rule. The visible change is that a line reading
`has.dot:` is no longer offered in the suggestion list.

This is a **fix**, not a regression: the assembler would not have accepted
`has.dot` as a label either, so the old behaviour offered the reader a
completion that could not assemble. It is recorded here because it is a
user-visible change to a shipped behaviour with no bug report behind it.

`LabelScannerConsistency.TheCompletionScannerAgreesWithTheIndex` pins it,
reading the editor's installed completion source rather than calling the
index twice.

---

## 44. Two paint defects in the references list, found by reading screenshots

Both only at a large font, both fixed in `37c3406`; recorded because no
test caught either and the tests that exist still would not.

- The header reserved `fm.horizontalAdvance(name) + 80` and drew
  `"%1 - %2 found"`. At max zoom the widget was narrower than the string
  and the header read `loop - 4`, with `found` clipped mid-word. The width
  is now measured from the same function that draws it, and the header is
  elided rather than clipped.
- The `def` tag is right-aligned in its band; neither the reserved width
  nor the row's elide allowed for it, so at max zoom it was painted **on
  top of** the row text. Both now subtract it.

A third, smaller one: the line-number column was a hard-coded `"0000"`,
wider than the real numbers at a small font and narrower at a large one.

**What this says about the tests.** The widget-level tests assert counts,
selection, offsets and visibility — everything except what the pixels
look like. A geometry assertion would have caught the header (the widget
was narrower than the text it drew) but not the overdraw, which is two
`drawText` calls landing in the same place. Reading the images is
currently the only thing that finds this class of defect, and it has now
found it twice (see **36** for the suggestion list's elide).

---

## 45. WITHDRAWN: the rename's `beginEditBlock` was never actually tested

**This entry previously claimed that `Editor::applyRename()`'s
`beginEditBlock`/`endEditBlock` "cannot be shown to do anything", on the
strength of a negative control that showed identical numbers with and
without it. That claim is wrong, and so was the control.**

### What actually happened

The control was applied by a script that did
`s.replace("    cursor.beginEditBlock();", ..., 1)` on
`main_editor.cpp`. There are **five** edit blocks in that file, and the
first one is in `replaceCurrentMatch()` at line 881. `applyRename()`'s is
at line 1975. So the mutation removed the **find-and-replace** edit block
and left the rename's untouched. The three instruments were measuring
unmutated code, and of course they agreed.

Identical numbers from a control are indistinguishable from a control
that never took effect. The only thing that tells them apart is proving
the mutation reached the binary, which was not done.

### The corrected measurements

On a **bare** `QTextDocument` and `QTextCursor` — no `Editor`, no
highlighter, nothing of Atlas's — doing three replacements at three
discontiguous positions, last to first, which is the shape
`applyRename()` has (`atlas-verify/harness/undoprobe`):

| variant | `undoCommandAdded` | `document->undo()` calls to restore | restored |
|---|---|---|---|
| no edit block | **3** | **3** | yes |
| inside `beginEditBlock` | **1** | **1** | yes |

Qt does **not** coalesce these edits on its own. The block is what groups
them.

With the control applied to the right lines — mutation shown as a diff
inside `applyRename()`, ninja shown rebuilding `main_editor.cpp.o` and
relinking `kites_test` — `LabelRename.OneCtrlZUndoesTheWholeRename`
**fails by name**, reporting 3 undo commands and 3 Ctrl+Z presses where
it requires 1 and 1.

### What is corrected

- `0203ac7`'s message says "One block, so one Ctrl+Z puts every site back
  at once". That is **correct**; the earlier version of this entry wrongly
  called it a wrong attribution.
- `aabad96`'s message says the control "cannot bite" and that Qt coalesces
  the edits by itself. **Both are wrong.** The control bites; Qt does not
  coalesce.
- The test did not need fixing. Counting undo commands and Ctrl+Z presses
  was already the right instrument — it simply had nothing to detect while
  the mutation was landing in another function.

### The lesson, which is not "be careful"

A negative control that comes back green has two explanations — the test
is blind, or the mutation never arrived — and reporting the first without
ruling out the second is how a passing control becomes evidence for a
false claim. Every control from here on shows the diff of what it changed
and the build output proving it was recompiled, which is what found this.

---

## 46. Shift+F12 and F2 are linear in the number of references

Measured at HEAD on a 10,003-line document with 5,000 references to one
label, through `navperf` under the hermetic wrapper, five samples each:

| action | median | range |
|---|---|---|
| F12, index warm | 521 µs | 427 – 554 |
| F12, index cold (after an edit) | 30,833 µs | 30,387 – 31,545 |
| Shift+F12 open, 5,000 references | 19,104 µs | 19,023 – 19,715 |
| F2 apply, 5,001 sites rewritten | 68,166 µs | 67,370 – 68,831 |
| Ctrl-hover, steady state | 291 µs/move | 230 – 293 |

The cold F12 is **41**, the full rescan, and nothing new.

The other two are their own. `showReferencesAtCaret()` builds a `Row` for
every reference — including a trimmed copy of its line — before the list
draws at most ten of them, so opening on 5,000 references costs 19 ms.
`applyRename()` rewrites every site through a `QTextCursor`, so 5,001
sites cost 68 ms.

**Not fixed, and arguably not worth fixing.** Both are deliberate
one-off actions on a keypress, not a typing path, and 5,000 references to
a single label is far outside what an Atlas teaching program contains —
the whole 44-file corpus holds 18 labels. They are recorded so that the
numbers exist if a larger document ever turns up, and so the shape is on
record: the references list could build rows lazily, and the rename could
batch its edits, if either ever mattered.

Typing is unaffected and was measured against `bda9dde` interleaved,
five samples each on both documents: +1.5% on the 877,043-character file
and +4.6% on the 20,000-line file, both well inside the baseline's own
spread (35.5 ms and 8.8 ms respectively).

---

## 47. Corrections to the Stage B first-half report

Three statements in that report were wrong. The report itself cannot be
edited, so they are corrected here.

1. **The commit list omitted `4b88bbd`.** The first half holds five
   commits, not four: `4b88bbd` (the `QStringView` tokeniser, and the
   correction that became **42**), `ea7d88e` (Shift+F12 wired), `c0062a4`
   (the suggestion-list test driven directly), `37c3406` (the references
   list's header and tag), `d6667f5` (the docs). Entries **41** and **42**
   were both added by `4b88bbd`, which is exactly the commit the list
   dropped.

2. **`a481e5a` was described as coming from the hermetic-verification
   stage.** It does not. It is
   `fix(processor): a clocked state with no program counters is not
   subscripted`, dated 14 Nov 13:18 — the earlier **Debug Run crash fix**,
   touching `src/processor/processor_manager.cpp` and adding
   `test/processor_highlight_test.cpp`. No document repeats the wrong
   version; the one mention of it, in **32**, already describes it
   correctly.

   The processor gate was also stated against the wrong base. It is now
   `git diff bda9dde..HEAD -- src/processor`, which must be empty, and is.

3. **The transcript comparison was reported as "845 differing rows"
   before being resolved.** That was a comparison of two different things.
   `transcript` emits 1,065 rows: 220 **role** rows, each beginning `R`,
   and 845 **positional** rows, which dump every `QAction` the window owns
   by index across the 21 execution steps from `00-start` to `17-final`.
   The baseline file, `role_transcript_f6edbac_hermetic.tsv`, contains the
   220 role rows and nothing else. The gate compares the role rows against
   that file — the same file and the same rows — and they match byte for
   byte: md5 `b518ba12776391c34cc520e77585d013`, 0 differing rows. The 845
   positional rows have no counterpart in the baseline and are not a
   behavioural difference.

   > **RETIRED**, see the note in 34. `b518ba12…` is the signature of a
   > run whose program file could not be opened, not of correct
   > behaviour, and it was matched here because the comparison was made
   > against a file produced the same way. The gate is now an A/B
   > between two freshly built trees in one session.

---

## 48. `contextAt()` stops short of the lexer on characters it cannot read

`Lexer::getNextToken()` ends in `else { skipLine(); return INVALID; }`:
any character it has no case for — `'`, `%`, `@`, `+`, `[` — makes it
abandon the rest of the line. `LabelIndex::tokenise()` now does the same,
because its only job is to agree with the assembler.

`EditorCommands::contextAt()` does **not**. It stops at "a single quote
does not open a string" and keeps scanning the line.

**Why.** `contextAt()` has two readers with different needs. Bracket
matching and the completion trigger want the assembler's view. Auto-pairs
want the editor's: `'` is a pair opener that the editor deliberately
offers, and if a stray `'` ended the line for `contextAt()`, typing `(`
anywhere after one would stop pairing. That would be a visible regression
in a shipped, tested behaviour (`editor_pairs_test.cpp` has five cases for
`'`) in exchange for agreeing with an assembler that has already given up
on the line.

**What it costs.** On a line the assembler discards, the editor may still
pair a bracket, match a bracket, or offer a completion. The line does not
assemble either way, so nothing the editor does there can be read back.

**OPEN**, in the sense that the split is a judgement rather than a
certainty. If the pair rules ever grow their own line scanner, the two
jobs come apart and `contextAt()` should simply mirror the lexer.

---

## 49. The highlighter cannot be checked by rendering in `kites_test`

`kites_test` loads no theme resources, so
`ThemeManager::getSyntaxFormat()` returns a default-constructed
`QTextCharFormat`. `QSyntaxHighlighter::setFormat()` with an empty format
adds **no format range at all**, and `block.layout()->formats()` comes
back empty for every line.

Two instruments were tried and both are useless there:

- comparing the painted colour against the Comment colour — every format
  compares equal to every other, so the assertion passes whatever the
  rules say. A test written this way **passed against a highlighter with
  no `;` rule in it**, which is how this was found.
- asking whether any format range covers the offset — that fails even for
  `#`, because no range exists.

What is testable without a theme is the **rule**.
`SyntaxHighlighter::commentPattern()` is public and static so the test and
the highlighter share one definition, and
`CommentSyntaxHighlighter.TheCommentRuleCoversBothCharacters` drives it
over eight lines. The rendering is checked in
`atlas-verify/harness/commentshots`, which has the resources: the `#`
tail, the `;` tail and a character typed into a `;` comment all come out
the same colour in all four real themes, and the inside of a string comes
out uncoloured.

This is the same constraint as the contrast and zoom work — see the note
in AGENTS.md — reached from a different direction.

---

## 50. The index and the lexer disagree about which token is the reference on a malformed line

`alphabetprobe` found two lines where the two agree about how far to scan
but not about **which** token is the label reference:

| Line | Lexer | Index |
|---|---|---|
| `    , j loop` | `j` is the reference — an identifier straight after a `COMMA` is one, so `loop` follows a `LABEL_REF` and is not | `loop` is the reference |
| `    j j loop` | the second `j` is the reference, for the same reason after an `OPCODE` | `loop` is the reference |

Both come from the same rule, which is the one the whole index is built
on: an identifier in operand position — straight after an opcode or a
comma — is a label reference. The lexer applies it to the *immediately*
preceding token and then stops; the index keeps `afterCommaOrOpcode` set
for the rest of the line, so every later identifier is still "in operand
position".

**Not changed.** Both lines are malformed: a leading comma is a syntax
error, and `j j loop` passes a mnemonic where an operand belongs. Making
the index drop the flag after the first operand would change how it reads
`beq x1, x2, loop` — three operands, the last of which is the reference —
which is the single most common real form. The gain is agreement on lines
that do not assemble; the risk is the main path.

**What it costs.** On such a line the index may report `loop` as a
reference where the assembler reports `j`. A rename of `loop` would then
rewrite a line the assembler rejects anyway.

Recorded because the probe found it and because it is the only known
place where the two disagree at all; `alphabetprobe` prints it on every
run as `[reference differs on the plain line: ...]` so it cannot quietly
grow.

---

## 51. Nine RV64 `*w` instructions have no entry in `instruction_syntax_map`

`addw`, `addiw`, `subw`, `sllw`, `slliw`, `srlw`, `srliw`, `sraw`,
`sraiw` are in `valid_instructions`, so the lexer makes them `OPCODE`
tokens, and they have encodings in
`src/common/instructions.cpp:42-43`, `:94` and the surrounding table.
They have **no entry in `instruction_syntax_map`**
(`src/common/instructions.cpp:763`), so the parser has no syntax to match
and rejects every use.

**The mechanism of the empty message.** `Parser::parse()` does
`instruction_set::instruction_syntax_map[currentToken().value]`
(`src/assembler/parser.cpp:332-333`). `operator[]` on a missing key
default-constructs an empty `std::vector<SyntaxType>`, the loop over it
does nothing, and the diagnostic is built from that empty list — which is
why the error reads `Invalid syntax: Expected: ` with nothing after the
colon.

Found by `atlas-verify/harness/isaprobe`. Nothing is fixed here; the nine
are marked `unsupported` in `instruction_docs.json` and no longer offered
by `instructions.json`.

---

## 52. `fence`, `fence_i`, `fclass.s` and `fclass.d` have no code-generator path

All four lex and parse cleanly and then throw out of the code generator
with `Invalid instruction type: <name>`. `fence` and `fence_i` are
declared `SyntaxType::PSEUDO`; `fclass.s` and `fclass.d` are
`O_GPR_C_FPR` and reach the generator as ordinary `ICUnit`s.

`fclass` is the odd one: the **execution** side is fully implemented —
`AluOp::FCLASS_S` at `src/processor/alu.cpp:591` builds the complete
10-bit class mask, and `FCLASS_D` at `:971` does the same — so only the
encoding step is missing.

---

## 53. `call` and `tail` are declared but not implemented

Both are in `valid_instructions` and both are declared
`SyntaxType::PSEUDO` in `instruction_syntax_map`, so the parser routes
them to `Parser::parse_pseudo()`. That function
(`src/assembler/parse_formats/pseudo_formats.cpp`) has branches for `j`,
`jr`, `ret`, `jalr`, `mv`, `neg`, `not`, `li`, `la` and the rest — and
**no branch for `call` or `tail`**. The call falls through, returns
false, and the parser reports `Expected: call <text label>`, text
generated from the syntax map rather than from any implementation.

No operand form works; the label form was tried explicitly. The
misleading `} // call` comment at `pseudo_formats.cpp:1044` actually
closes the `ret` branch.

---

## 54. `fmv.s.x` and `fmv.x.s` are one line each from working

Unlike the other 34 rejected aliases, these two are **already in the
name-to-enum table**:

```cpp
{"fmv.x.s", Instruction::kfmv_x_w},   // src/common/instructions.cpp:330
{"fmv.s.x", Instruction::kfmv_w_x},   // src/common/instructions.cpp:332
```

so the encoder and the processor would handle them as `fmv.x.w` /
`fmv.w.x` — `AluOp::FMV_X_W` at `src/processor/alu.cpp:622` and
`FMV_W_X` at `:629`. They are missing only from `valid_instructions` and
from `instruction_syntax_map`, so the **lexer** rejects them before
anything else sees them (`Unexpected token: fmv.x.s`).

Recorded because it is a different kind of gap from the other 34, which
have no machinery behind them at all.

---

## 55. The fence-instruction spelling is `fence_i`, not `fence.i`

`valid_instructions` and `instruction_syntax_map` carry **`fence_i`**
with an underscore. The RISC-V spelling `fence.i` is absent everywhere,
so a reader who types the standard form gets `Unexpected token`.

`fence_i` is also not offered by `instructions.json` and does not reach
the code generator (see **52**), so the underscore spelling is currently
unusable as well — but it is the one the tables name.

---

## 56. `kites-cli` segfaults in batch mode on every program it processes

**Measured**, not assumed. `QT_QPA_PLATFORM=offscreen ./build/kites-cli <file>`
over the first ten `examples/*.s`:

| input | exit |
|---|---|
| ten example programs, `cache_matrix_mul_3x3.s` … `fp_FL_FS_hazard.s` | **10 of 10 SIGSEGV** (rc 139) |
| no argument at all | **SIGSEGV** (rc 139) |
| a path that does not exist (`/nonexistent/nope.s`) | **rc 1, clean exit** |

The last row is why the heading says "every program it processes"
rather than "every input": the file-open check runs first and returns 1
without ever constructing a processor. Everything that gets past it
dies. The result is the same with and without
`QT_QPA_PLATFORM=offscreen`, which is expected — the crash is a missing
`QApplication`, not a missing display.

**Mechanism.** `CMakeLists.txt:135` is

```cmake
target_compile_definitions(kites_cli PRIVATE DISABLE_GUI)
```

`PRIVATE` applies the definition to `cli/main.cpp` and `cli/repl.cpp`
only. `kites_core` is built once, **without** `DISABLE_GUI`, and
`kites_cli` links that library — so the CLI gets a processor compiled
with its GUI parts in, constructs a `QGraphicsScene` with no
`QApplication`, and dies.

**Is this the same as AGENTS known issue 1?** It is the same *fault* —
constructing a processor builds a `QGraphicsScene` that needs a
`QApplication` — reached by a different route and with a different fix.
Issue 1 is about `kites_test`, and was fixed by giving the test harness
an offscreen `QApplication` in `test/test_main.cpp`. Here an
`QApplication` is not the intended answer: the CLI is meant to be
GUI-free, and the define that would make it so never reaches the code
that needs it. A `QApplication` in `cli/main.cpp` would hide the problem
rather than fix it.

Not fixed here: `CMakeLists.txt` and `src/` are outside this branch's
scope. The processor probes in this work use a harness with an offscreen
`QApplication` instead.

---

## 57. `fcvt.s.d` and `fcvt.d.s` are never mapped to an ALU operation

Both assemble: they are in `valid_instructions`, in
`instruction_syntax_map` as `O_FPR_C_FPR`, and have encodings at
`src/common/instructions.cpp:169-170`.

**Mechanism**, end to end, every step re-verified:

1. `src/processor/rvss/rvss_control_unit.cpp:120-124` — `case 0b1010011:`
   (F-type R) sets `reg_write_ = true` from the **opcode alone**. funct7
   is never examined here, so an unmapped F-type op still asserts the
   register write.
2. `:591` — `GetAluSignal`'s `case 0b1010011:` switches on funct7.
   `fcvt.s.d` is funct7 `0b0100000` and `fcvt.d.s` is `0b0100001`
   (`src/common/instructions.cpp:169-170`). **Neither has a case, and
   that switch has no `default`**, so control falls out of it.
3. `:972` — and reaches the function's final `return alu::AluOp::NONE;`.
4. `src/processor/alu.cpp` has three entry points — `execute` (`:18`),
   `fpexecute` for single precision (`:282`) and `dfpexecute` for double
   (`:661`). A D-extension instruction reaches **`dfpexecute`**
   (`rvss_processor.cpp:392`), whose `double result = 0.0;` is at
   `:668` and whose op switch ends in `default: break;` at `:1028`,
   leaving `result` untouched for `NONE`.
5. `:1050-1052` — `uint64_t result_bits = 0;` then
   `memcpy(&result_bits, &result, sizeof(result))`: an all-zero 64-bit
   result.
6. `isDInstruction` returns true for both (`instructions.cpp:1137-1145`),
   so `WriteBack()` routes to `WriteBackDouble()`, which honours
   `GetRegWrite()` — and the zero is committed.

So the destination is **overwritten with +0.0, all 64 bits**. It is not
left alone, and it does not keep a previous value.

**This is a class of defect, not two instructions.** Any F-type funct7
with no case in that switch falls through to `AluOp::NONE` and silently
writes zero, because step 1 asserts the write from the opcode alone.

The liveness sweep over all 146 supported instructions found nine more
broken float instructions, in two further shapes:

- **issue 60**, `fcvt.d.lu`: the **mirror** of this one, not another
  instance of it. There the control unit names the operation correctly
  and the **ALU** has no case for it — the slot where `FCVT_D_LU`
  belongs holds a mislabelled duplicate `case AluOp::FCVT_S_LU:`. Same
  symptom, opposite half.
- **issue 61**, the eight fused multiply-adds: a third shape again —
  they are never routed to the float path at all, so nothing is
  written and the destination keeps its old value rather than a zero.

Of 143 instructions swept, 132 were live and these 11 were not.

**Scope of the claim.** The first evidence here was weak and has been
replaced: it used destinations that were already zero, so it could not
tell "writes 0" from "does not write". Four preloaded-destination pairs
now settle it. Each runs the same program twice, once with the
conversion and once without, because `DebugRun()` runs to completion:

| destination preloaded with | after the conversion |
|---|---|
| f2 = 7.0 `0x401C000000000000`, then `fcvt.s.d f2, f1` | **`0x0000000000000000`** |
| f2 = 123.0 `0x405EC00000000000`, then `fcvt.s.d f2, f1` | **`0x0000000000000000`** |
| f8 = 9.0f `0x41100000`, then `fcvt.d.s f8, f7` | **`0x0000000000000000`** |
| f8 = 123.0f `0x42F60000`, then `fcvt.d.s f8, f7` | **`0x0000000000000000`** |

The result does not depend on the old value: 7.0 and 123.0 give the same
all-zero word. Two positive controls show the method is not blind —
`fcvt.d.w f2, x1` over a preloaded f2 = 7.0 writes −1.0, so a real write
is detected; and the preload alone, with nothing after it, still reads
7.0, so the sentinel is not being lost some other way. Downstream code
sees a clean `+0.0`: `fadd.s f3, f2, f2` on the result yields `+0.0`, no
NaN and no trap, so the corruption propagates as ordinary arithmetic.

**Why a control-unit fix alone is not enough.** Both ALU cases are also
wrong in a second, independent way. `alu.cpp:1004` is
`static_cast<float>(ina)` and `:1010` is `static_cast<double>(ina)`,
where `ina` is the raw `uint64_t` (`dfpexecute(AluOp, uint64_t ina,
...)` at `:661`) — they convert the **bit pattern as an integer**, not
the stored value. `dfpexecute` memcpy's `ina` into `double a` at `:665`
for exactly that purpose, and neither case uses `a`. Mapping the funct7
would make these two instructions run and still produce wrong answers,
so this is not a one-line fix.

This is the most consequential gap found: the instruction assembles,
runs, and produces a wrong answer rather than an error.

**Marked in the data file.** `resources/instructions/instruction_docs.json`
(v4) carries an optional `caveat` string on both entries:

> Known bug in this simulator: the control unit never maps this
> instruction, so it assembles but always writes 0 (known_issues 57).

Both stay marked **supported**, because they do assemble, and the test
suite only assembles — nothing it can do would observe the zero. The
caveat is the only place the file admits it.
`InstructionDocs.EveryCaveatIsOnASupportedEntryAndNamesARealKnownIssue`
pins that a caveat sits on a supported entry and that the number it
cites has a heading here.

---

## 58. Float literals lose precision in the lexer

`.double` and `.float` store non-representable decimal fractions to
about six decimal places. Measured by loading each literal back with
`fld` / `flw` on the real processor:

| literal | stored bits | value | correct |
|---|---|---|---|
| `.double 0.5` | `0x3FE0000000000000` | 0.5 | exact ✓ |
| `.double 3.0` | `0x4008000000000000` | 3.0 | exact ✓ |
| `.double 0.125` | `0x3FC0000000000000` | 0.125 | exact ✓ |
| `.double 1.0e10` | `0x4202A05F20000000` | 1e10 | exact ✓ |
| `.double 0.1` | `0x3FB999AA60913A50` | **0.100001** | `0x3FB999999999999A` |
| `.double 123.456789012345` | `0x405EDD3C0C1FC8F4` | **123.45679** | `0x405EDD3C07FB4C69` |
| `.float 0.1` | `0x3DCCCD54` | **0.100001f** | `0x3DCCCCCD` |
| `.float 1.5` | `0x3FC00000` | 1.5 | exact ✓ |

Only values that are exactly representable survive. One step of the
mechanism is visible at `src/assembler/lexer.cpp:225`:

```cpp
return {TokenType::FLOAT, std::to_string(std::stod(value)), ...};
```

`std::to_string(double)` is `%f` with six digits after the point, so the
token carries a decimal string rather than the parsed value and the
parser re-parses that string (`src/assembler/parser.cpp:230`). That
accounts for the six-decimal shape of the loss, though not for every
digit of the two inexact results above, so the round trip may be lossy in
more than one place.

**Nothing documents this**, so it is not a disagreement with a summary —
it is a finding. A teaching simulator that stores `0.1` as `0.100001`
will confuse anyone writing a floating-point exercise.

---

## 59. A label named like a register cannot be referenced, and the error says "Could not open the file"

Two defects stacked: a name collision that makes a valid-looking
program fail, and an error path that replaces the real complaint with a
meaningless one. Found by accident — the liveness sweep used `s9` as a
data label, and the resulting message sent the investigation the wrong
way for several minutes.

**Minimal reproducer.**

```asm
.data
  s9: .float 9.0
.text
main:
  la x5, s9
```

Through `assemble(std::istream &)`, the route every in-memory caller
uses, this throws:

```
Could not open the file.: iostream error
```

Nothing is wrong with any file. Renaming the label to `snine` makes the
program assemble.

**Mechanism, part 1 — the collision.** `src/assembler/lexer.cpp:129`
tests `IsValidGeneralPurposeRegister(value)`, and `:133`
`IsValidFloatingPointRegister(value)`, **before** the operand-position
rule at `:151-155` that would make the identifier a `LABEL_REF`. So an
identifier whose text matches a register is lexed as a register
wherever it appears, and the parser's format match then fails.

Proved at the token level (`atlas-verify/harness/cur_tokdump`):

| source | token for the last operand |
|---|---|
| `la x5, s9` | `GP_REGISTER` |
| `la x6, safe` | `LABEL_REF` |

The *definition* is unaffected — `s9:` lexes as `LABEL`, and a program
that defines `s9` without referencing it assembles cleanly. Only the
reference breaks.

The rule at `:151-155` carries a comment explaining that an identifier
in operand position is a label reference "even if its text happens to
match an instruction mnemonic, e.g. `j add`". That protection was
placed above the `isValidInstruction` check but **below** the two
register checks, so mnemonic-named labels work and register-named
labels do not.

**Affected names.** Every one tested fails: `x5`, `f1`, and the ABI
aliases `zero`, `ra`, `s9`, `t0`, `a0`, `fa0`. The alias table is
`src/common/register_names.h:16-19` (`zero ra sp gp tp t0-t6 s0-s11
a0-a7`) plus `x0-x31`, `f0-f31` and the FP aliases — roughly 128
reserved words, none of them documented as reserved. `safe` works.

It is not specific to `la`: `beq x5, x5, s9` fails the same way.

**Mechanism, part 2 — why the message is useless.** The parser does
produce a good diagnostic. Assembled from a file on disk, the same
program reports:

```
Invalid syntax: Expected: la <reg>, <text label>
```

and `beq x5, x5, s9` reports its own `Expected: beq <gp-reg>,
<gp-reg>, <imm> or beq <gp-reg>, <gp-reg>, <text-label>`.

The message is lost on the in-memory path. Every one of those error
sites quotes the offending source line by **re-reading it from disk** —
`GetLineFromFile(filename_, token.line_number)`, at `parser.cpp:129`,
`pseudo_formats.cpp:77`, `:249`, `:918`, `csr_formats.cpp:88`,
`f_d_formats.cpp:466`, `:488` and twelve places in `i_m_formats.cpp` —
32 call sites in all.
`GetLineFromFile` opens the path and throws when it cannot
(`src/utils/utils.cpp:94-98`). A stream has no path, so building the
error message throws `std::ios_base::failure("Could not open the
file.")`, and that escapes in place of the diagnostic.

**Who actually hits this — a correction.** An earlier version of this
entry said the editor gets "Could not open the file" for any syntax
error. That is **wrong**, and the claim was never checked against the
app's own path. It was checked afterwards:

`assemble(std::istream &)` has **no caller anywhere in `src/`**. All
three real callers use the file-path overload:

| caller | route |
|---|---|
| `src/ui/main_window/main_window.cpp:844-847` (Assemble / Run / Debug) | writes the editor buffer to `globals::temporary_assembly_file_path`, then `assemble(<that path>)` |
| `src/processor/processor_manager.cpp:79-82` | same temp file, same overload |
| `src/command_handler/command_handler.cpp:187` (the CLI) | `assemble(filePath)` |

Because a real file is on disk, `GetLineFromFile` opens it and the
diagnostic survives. What the user sees for a bad line is therefore the
*parser's* message, not an iostream failure: `assemble()` dumps every
real per-line error to `globals::errors_dump_file_path`
(`assembler.cpp:93` → `DumpErrors`, which writes `error.message`
verbatim) and throws `"Failed to parse file: <path>"` (`:98`).
`main_window.cpp:861-862` then feeds that dump to
`EditorTab::setErrorLinesFromFile` for the inline squiggles and shows
`e.what()` in the message box.

So the masked-diagnostic half reaches only **the unit tests and the
out-of-tree harnesses** — the eight test files that assemble from a
`std::istringstream`, and `atlas-verify/harness/{cpuprobe,livesweep}`.
Not the GUI, and not the CLI.

**Measured through the app's own path**
(`atlas-verify/harness/apppath` replays `main_window.cpp:844-847`
exactly — write the buffer to the temp file, call the file overload,
catch, read the dump):

| program | message box | editor's inline error |
|---|---|---|
| `addi x5, x6` | `Failed to parse file: <...>/vm_state/temp.asm` | `Invalid syntax: Expected: addi <gp-reg>, <gp-reg>, <imm>` (line 3, cols 3-7) |
| `la x5, s9` | same | `Invalid syntax: Expected: la <reg>, <text label>` (line 5) |
| `addi x5, x6, 3` | none | `"errorCode": 0` |

No "Could not open the file" anywhere. The message box is generic, which
is its own (much smaller) complaint, but the real diagnostic reaches the
user inline with the right line and columns.

That is still worth fixing, because it is what makes a failure in those
harnesses unreadable — this entry exists only because a harness hit it
and the message sent the investigation the wrong way. But it is a
testing-path defect, not a user-facing one, and the collision in part 1
is the half that reaches the app.

**Not fixed here.** `src/` is outside this branch's scope. A fix would
move the two register checks below the operand-position rule (part 1)
and make the error sites use the source text the lexer already holds
rather than re-reading the file (part 2); the second would repair every
in-memory diagnostic at once.


---

## 60. `fcvt.d.lu` writes zero: the ALU case is labelled `FCVT_S_LU` by mistake

The mirror of issue 57. There the control unit never names the
operation; here it names it correctly and the **ALU has no case for it**.

**Measured** (`atlas-verify/harness/livesweep`, real `RVSSProcessor`,
destination preloaded with `0x40934800ABCDEF12` and `x5 = 965`):

| program | f1 after |
|---|---|
| `fcvt.d.lu f1, x5` | **`0x0000000000000000`** |
| `fcvt.d.l f1, x5` | `0x408E280000000000` (965.0) |
| `fcvt.d.wu f1, x5` | `0x408E280000000000` (965.0) |
| `fcvt.s.lu f1, x5` | `0x44714000` (965.0f) |

Its two nearest neighbours work. It does not.

**Mechanism.** The control unit is fine, in both models:
`src/processor/rvss/rvss_control_unit.cpp:720-722` returns
`alu::AluOp::FCVT_D_LU` for funct5 `0b00011`, and
`src/processor/rv5s/rv5s_control_unit.cpp:355-356` does the same.

`src/processor/alu.cpp` has three entry points — `execute` (`:18`),
`fpexecute` for single precision (`:282`) and `dfpexecute` for double
(`:661`). A D-extension instruction reaches `dfpexecute`
(`rvss_processor.cpp:392`, from `ExecuteDouble()`), whose `switch (op)`
begins at `:699`.

In that switch the conversions run in order:

```
:844   case AluOp::FCVT_D_W:
:850   case AluOp::FCVT_D_WU:
:856   case AluOp::FCVT_D_L:
:862   case AluOp::FCVT_S_LU:     <- should be FCVT_D_LU
```

`FCVT_S_LU` already has its own case at `:482`, in `fpexecute`, where it
belongs and where it works. The one at `:862` is a **copy-paste slip
with the right body and the wrong label**:

```cpp
case AluOp::FCVT_S_LU:
{
    auto ula = static_cast<uint64_t>(ina);
    result = static_cast<double>(ula);   // `result` here IS a double
    break;
}
```

That body is exactly what `FCVT_D_LU` should do. Changing the label
alone would fix the instruction. Because the two labels sit in
different switches, the duplicate is not a compile error — it is simply
dead code in `dfpexecute`, since a single-precision op never arrives
there.

With no case, `FCVT_D_LU` reaches `default: break;` (`:1028`), leaves
`double result = 0.0;` (`:668`) untouched, and `:1050-1052` commits that
zero. The destination is **written with +0.0**, exactly as in 57 — not
left alone.

**Both models are affected.** `alu.cpp` is one shared file, and the rv5s
control unit maps the operation the same way, so rv5s selects the same
missing case.

Not fixed here: `src/` is outside this branch's scope.
`resources/instructions/instruction_docs.json` carries a `caveat` on the
entry so the documentation does not promise a working instruction.


---

## 61. The eight fused multiply-add instructions never write their destination

`fmadd`, `fmsub`, `fnmadd` and `fnmsub`, in both `.s` and `.d`, assemble
and run and leave the destination register exactly as it was. Not
zeroed, as in 57 and 60 — **not written at all**.

**Measured** (`atlas-verify/harness/livesweep`, real `RVSSProcessor`,
f1 preloaded with `0x40934800ABCDEF12`, five different source sets):

| program | f1 after |
|---|---|
| `fmadd.s f1, f2, f3, f4` | `0x40934800ABCDEF12` — **the sentinel, unchanged** |
| `fadd.s f1, f2, f3` (control, same preload) | `0x411C0000` (9.75) |
| `fmadd.d f1, f2, f3, f4` | `0x40934800ABCDEF12` — **unchanged** |

All eight behave the same across all five source sets.

**Mechanism — the dispatch, shared by both models.** The control unit is
not where this starts. `SetControlSignals` handles all four opcodes
(`src/processor/rvss/rvss_control_unit.cpp:126-133`, setting
`reg_write_ = true`), and `rs3` really is decoded and read
(`rvss_processor.cpp` `ExecuteFloat()`: `rs3 = (instruction >> 27) &
0b11111`, `reg3_value = registers_.ReadFpr(rs3)`).

The break is one level up, in the routing:

- `isFInstruction` (`src/common/instructions.cpp:1085`) and
  `isDInstruction` (`:1120`) switch only on opcodes `0b0000111`,
  `0b0100111` and `0b1010011`. **Neither has a case for `0b1000011`,
  `0b1000111`, `0b1001011` or `0b1001111`**, so both return false for
  every FMA.
- `Execute()` (`rvss_processor.cpp:188-196`) and `WriteBack()`
  (`:834-842`) use those two predicates to choose the float path. With
  both false, an FMA falls through to the generic integer path.
- The generic `switch (opcode)` in `WriteBack()` (`:856`) has no case
  for any FMA opcode either, so **no register is written**. That is why
  these read UNCHANGED rather than ZEROED: control never reaches a
  write, so there is no zero to commit.

**rv5s shares this half.** `rv5s_processor_h_nf.cpp:215-216` and
`rv5s_processor_h_f.cpp:285-286` call the same two shared predicates.

**Is fixing the predicates enough? No — and this is the 57 situation
again, one layer along.** Read, not run:

*The ALU implementations are correct.* `alu.cpp:330-348` (`fpexecute`)
and `:709-727` (`dfpexecute`) are

```cpp
case AluOp::FMADD_S:  result = std::fma(a, b, c);    break;
case AluOp::FMSUB_S:  result = std::fma(a, b, -c);   break;
case AluOp::FNMADD_S: result = std::fma(-a, b, -c);  break;
case AluOp::FNMSUB_S: result = std::fma(-a, b, c);   break;
```

and the same four for `_D`. Those match the RISC-V definitions, and —
unlike the broken conversions in 57 — they use the memcpy'd `a`, `b`,
`c`, not the raw `ina`. This half is sound.

*The ALU-op selection is not.* In rvss's `GetAluSignal`, only one of the
four opcodes has a top-level case at all:

| opcode | top-level case in `GetAluSignal` |
|---|---|
| `0b1000011` FMADD | `:586` — returns `FMADD_S` **unconditionally** |
| `0b1000111` FMSUB | **none** |
| `0b1001011` FNMSUB | **none** |
| `0b1001111` FNMADD | **none** |

`:586-589` sits directly under a `// TODO: correct this` comment
(`:584`) and ignores `funct2`, so `fmadd.d` would be executed as
`FMADD_S`. The block at `:871-882` that *does* distinguish `.s` from
`.d` by `funct2` is a **`funct7` case nested inside opcode `0b1010011`**,
so it can never be reached by an instruction whose opcode is
`0b1000011`. The three opcodes with no case fall through to the
function's final `return alu::AluOp::NONE;` (`:972`) — the same
fall-through as 57, with the same consequence once a write is reached.

So repairing `isFInstruction`/`isDInstruction` alone would, on rvss,
make **one** of the eight work (`fmadd.s`), run `fmadd.d` at the wrong
precision, and turn the other six from "writes nothing" into "writes
zero". That is a worse failure, not a better one.

**rv5s's control unit does not have this second hole.**
`rv5s_control_unit.cpp:96-97` covers the opcodes and `:382-387` selects
on `funct2` (`FMSUB_S`/`FMSUB_D`, `FNMADD_S`/`FNMADD_D`, …). On rv5s the
predicates may well be the only thing in the way.

Not fixed here: `src/` is outside this branch's scope. All eight entries
in `resources/instructions/instruction_docs.json` carry a `caveat`.


---

## 62. FIXED: `Reset()` ran on the UI thread while the processor thread was still executing

An intermittent abort, in two different shapes, from one cause.

**Reproduction.** 200 runs of `atlas-verify/harness/transcript` (which
drives the real `MainWindow`), each hermetic, on
`examples/ex_mem_fwd.s`, with the stock `vm_state/config.ini`:
`processor_type=single_stage` (so `RVSSProcessor`),
`hazard_detection=false`, `forwarding=false`, `cache_enabled=false`.

**6 of 200 aborted (3%).** Five were the cache assertion; the sixth was
something else, and the sixth is the one that explains both.

| shape | count |
|---|---|
| `cache.cpp:410` assertion, `getByteFromCache called on a cache line that is not present!` | 5 |
| glibc `malloc_printerr` — heap corruption | 1 |

Note that `cache_enabled=false` does not take the cache out of the
path. `MemoryController::readInstruction` (`memory_controller.cpp:95`)
is `return instruction_cache_.readWord(address);` unconditionally, and
neither `memory_controller.cpp` nor `cache.cpp` reads an enable flag at
all. Every instruction fetch goes through a cache whatever the config
says.

**Shape 1 — the assertion, on the processor's worker thread.** The
faulting thread is never the main thread:

```
Kites::Cache::getByteFromCache(unsigned long)        <- assert
Kites::Cache::readGeneric<unsigned int>(unsigned long)
Kites::Cache::readWord(unsigned long)
Kites::MemoryController::readInstruction(unsigned long)
Kites::RVSSProcessor::Fetch()
Kites::RVSSProcessor::Step()
Kites::RVSSProcessor::StepAndNotify()
Kites::RVSSProcessor::Run()
Kites::ProcessorManager::run()
Kites::ProcessorManager::runSlot()          <- queued slot invocation
QObject::event -> QThread::exec             <- a worker QThread
```

In all five, the **main** thread was inside `settle()`'s event loop
doing Qt painting (`QWidgetRepaintManager::paintAndFlush`,
`QWidgetPrivate::paintBackground`, `getOpaqueChildren`) — not in any
Kites code.

The assertion itself is a single-iteration invariant. `readGeneric`
(`cache.cpp:504-515`) does, per byte: if `findWay` says the line is
absent, `evictCacheLine` then `bringIn`, and then `getByteFromCache`.
Tripping the assert means the line was gone **again** between the
`bringIn` on `:511` and the lookup on `:513`.

**Shape 2 — the heap corruption, on the main thread, caught in the
act.** This one names the other party:

```
MainWindow::run(bool)
MainWindow::tryParseAndLoadProgram()
Kites::ProcessorManager::reset()
Kites::RVSSProcessor::Reset()
std::stack<Kites::StepDelta>::operator=(&&)
std::deque<Kites::StepDelta>::clear()
~StepDelta -> ~vector<Kites::RegisterChange> -> deallocate
malloc_printerr                              <- glibc: heap corrupted
```

`RVSSProcessor::Reset()` (`rvss_processor.cpp:1327-1352`) does two
things that matter here, with no lock and no check that the processor
has stopped:

- `memory_controller_.reset()`, which resets all three caches
  (`memory_controller.cpp:14-21`) — the exact mutation that would make
  a line vanish between `:511` and `:513`;
- `current_delta_.register_changes.clear()` and
  `undo_stack_ = std::stack<StepDelta>()`, while the worker thread is
  pushing into `current_delta_.register_changes` from `WriteBack()`.

So one call, from the UI thread, clears both the cache the worker is
fetching through and the delta vectors the worker is appending to.
Which shape you get depends on which it reaches first.

Every `ProcessorManager` connection is `Qt::DirectConnection`
(`processor_manager.cpp:38-64`), so a signal emitted on the worker runs
its slot on the worker, and a UI action runs on the UI thread — the two
meet on the same objects.

**Both sides of the race were caught in one core.** An earlier version
of this entry said no core catches a second thread inside the cache at
the instant of the assertion, and inferred shape 1's ordering from
shape 2. That caveat is **retired**. Hammering `reset()` in a tight loop
widened the window enough to catch the pair simultaneously:

| worker thread (asserting) | main thread, same instant |
|---|---|
| `Cache::getByteFromCache` | `vector<CacheChange::LineChange>::_M_move_assign` |
| `Cache::readGeneric<uint32_t>` | `Kites::CacheChange::operator=(&&)` |
| `Cache::readWord` | **`Kites::Cache::clearHistory()`** |
| `MemoryController::readInstruction` | **`Kites::Cache::reset()`** |
| `RVSSProcessor::Fetch` | `Kites::MemoryController::reset()` |
| `Step` / `StepAndNotify` / `Run` | `Kites::RVSSProcessor::Reset()` |
| `ProcessorManager::run` / `runSlot` | `Kites::ProcessorManager::reset()` |

The UI thread is inside `Cache::reset()` while the worker is inside
`Cache::getByteFromCache()`. Observed, not inferred.

**Stop is the dominant real-world trigger**, not a second Run.
`ProcessorBase::RequestStop()` *itself* called `Reset()`, so every press
of Stop (`main_window.cpp:306`) and every `~MainWindow`
(`main_window.cpp:996`) entered this race - and Stop is pressed while a
run is in progress by definition. That is why the transcript harness,
which triggers Stop, aborted at about 3%.

### The fix

Design (a)+(c), in `ed07da3` and `2178f46`, with the test in `6eb5412`.

`StopAndReset()` sets `stop_requested_`, wakes every place the worker
can block, waits on a new `run_state_mutex_`/`run_idle_condition_` pair
until the worker has left `Run()`, and only then takes `step_mutex_` and
clears. The wait is bounded at 3 s; on timeout **nothing** is cleared,
because a stuck UI is better than a corrupted processor.
`RequestStop()` no longer calls `Reset()` while holding `pause_mutex_` -
that was the lock-ordering hazard. The order is now one-way,
`run_state_mutex_` -> `step_mutex_`, never both held at once, and it is
written down as an invariant at the declaration site. `Undo()` and
`Redo()` take `step_mutex_` in both processors.

A second hole had to be closed before any of that held. A run is started
by posting `runSlot` to the worker; one still queued when a stop arrives
starts *afterwards*, because `Run()` opened with `ClearStop()` and so
discarded a stop that predates it. The processor had just been reset and
began stepping again, putting `Reset()` back alongside `Run()` - that is
what dereferenced an empty `last_breakpoint_pc_` in the rv5s loop and
what put the cache assertion back. `StopAndReset` bumps a stop epoch and
publishes the stop in one critical section; `requestRun` snapshots the
epoch on the UI thread, and `BeginRun` refuses any run whose epoch is
stale or that arrives while a stop is in flight (`stop_in_progress_`).

**The clear itself moved, in `3e44e05`, `663cb06` and `ae3c832`.**
`Run()` no longer opens with `ClearStop()` at all; that call is gone from
all four run loops. `ClearStopLocked()`
(`src/processor/processor_base.cpp`), which requires `run_state_mutex_`,
is now the only place a stop is ever cleared, and two entry points reach
it: `BeginRun()` for a run requested from the UI, and `MarkRunEntered()`
- via `RunScope` - for one entered directly by the REPL, the tests or a
harness. The clear therefore happens in the *same* critical section that
checks the epoch, so a stop either lands before the run is approved and
cancels it, or after the clear and survives. There is no third outcome.

The `ClearStop()` this entry described was check-then-act across an
unlock: it tested the epoch under `run_state_mutex_`, released the lock,
and only then wrote `stop_requested_ = false`. A stop landing in that
window was wiped and the worker parked in the untimed pause wait
(`rvss_processor.cpp:136`, `rv5s_processor_base.cpp:33`), which presents
as a hung Stop button. Measured on a patched out-of-tree copy, debug-run
stop cycles with a 3 s timeout: **47 of 60 (rvss) and 48 of 60 (rv5s)**
timed out with a 100 ms delay injected between the guard and the writes,
against 8 of 60 and 1 of 60 with the same delay injected *before* the
guard. The same delay moved across the guard is the whole difference, so
the window is guard-to-write and nothing else. With the fix in, 0 of 500
per processor. The 50 ms stop re-assert loop that had been masking this
is deleted; worst-case stop latency is 1 ms (rvss) / 0 ms (rv5s) either
way, so it bought nothing.

`663cb06` also makes the REPL report a failed `stop`/`reset` instead of
discarding the result (`command_handler.cpp:296`, `:309`), and disarms
`start_paused_` when a run is refused, so a Debug Run cancelled by a stop
no longer leaves the *next* plain Run silently paused. `ae3c832` adds
`ResetRace.DirectRunAfterResetStillExecutes`, which pins the direct-run
path: `reset()` -> `loadProgram()` -> `run()` executed nothing at all
(`pc == 0`, both processor types) while the stale stop survived.

### Evidence

| | before | after |
|---|---|---|
| reset hammer, 1000 cycles x 200 resets, per processor | 20 of 20 runs aborted on cycle 1 | rvss 1000 clean, rv5s 1000 clean |
| hang stress, 500 cycles per processor, 5 s watchdog | rv5s aborted at cycle 6 | 0 hangs, 0 aborts, 0 pending-run restarts |
| 308-iteration driver | 2 of 14 processes died | 300 iterations, 0 aborted |
| transcript, 200 runs | 6 aborted | 200 clean |
| `ResetRace.*` in `kites_test` | aborts the binary, 3 of 3 | passes, 50 of 50 runs |

The harness now distinguishes an abort from a result, so a run that dies
is not read as a transcript difference.

**Still open, deliberately** - see issue 63: the UI thread reads
worker-owned state through raw pointers during a run, and `breakpoints_`
and `last_breakpoint_pc_` are read in the `Run()` loop outside
`step_mutex_`. None of that is fixed here.


---

## 63. The UI thread reads worker-owned processor state during a run

Issue 62 fixed the *writes*: `Reset`, `Undo` and `Redo` from the UI
thread can no longer land while the worker is inside `Run()`. The
**reads** are still unprotected, and so is one remaining write.

**Raw pointers handed straight to the views.** `ProcessorManager`
exposes the processor's own objects and `MainWindow` stores them in
widgets that repaint while the worker is running:

| site | what is handed out |
|---|---|
| `src/ui/main_window/main_window.cpp:104` | `getRegisterFile()` -> `RegisterContainer` |
| `:907` | `getRegisterFile()` again, on processor change |
| `:798` | `getMemoryController()` -> `MemoryTab` |
| `:800` | `getMemoryController()` -> `CacheTab` |
| `:911`, `:916` | the same controller, on processor change |

Every register the worker writes in `WriteBack()`, every cache line it
touches in `Fetch()`, can be read concurrently by a paint on the UI
thread. Nothing synchronises the two.

**Profiler counters.** `src/ui/processor_tab/vm_state_table_model.cpp:81-91`
reads `getProgramCounter()`, `getCycles()`, `getCPI()` and `getIPC()`
from the UI thread while the worker increments them.

**`breakpoints_`, and `last_breakpoint_pc_` with it.** A plain
`std::vector<uint64_t>` at `processor_base.h:104`, written by
`SetBreakpoints`/`AddBreakpoint`/`RemoveBreakpoint` from the UI thread
and read inside the `Run()` loop - `rvss_processor.cpp:124`, `:1074`,
`rv5s_processor_base.cpp:20` - **outside `step_mutex_`**. Protecting
only the setter would be false assurance, which is why issue 62 left it
alone: roughly 13 sites across three files, and the reads sit in the hot
loop.

`last_breakpoint_pc_` (`std::optional<uint64_t>`) is in the same
cluster, and it is the one with a demonstrated symptom. The rv5s loop
does

```cpp
if (last_breakpoint_pc_ && program_counter_ != *last_breakpoint_pc_)
```

which is safe single-threaded but not against a concurrent
`last_breakpoint_pc_.reset()`. A stress harness that called
`ProcessorManager::step()` during a run aborted on
`std::optional::operator*` -> `__glibcxx_assert_fail` inside
`RV5StageVM_Base::Run()`, i.e. an empty optional dereferenced between
the test and the use.

**How reachable is it?** Not from the UI as it stands: the transcript's
own role rows show `STEP`, `UNDO` and `REDO` with `enabled=0` during a
run, so the app disables exactly the actions that would provoke it. The
raw-pointer reads, by contrast, happen on every repaint and are
reachable now. The harness was corrected to respect the same
enablement, and the finding recorded here rather than dropped.

**Not fixed in this stage.** A fix means either a lock the `Run()` loop
participates in, or snapshotting state into the signal payload so the
views never touch live processor objects. Both are larger than the
reset fix and want their own stage.

---

## 64. `vm_state/` follows the working directory, so the app behaves differently per launch directory

`src/common/globals.cpp:11-25`:

```cpp
std::filesystem::path getWritableDataDirectory()
{
#ifdef __APPLE__
    ... HOME/Library/Application Support/Kites ...
#endif
    return std::filesystem::current_path();
}
std::filesystem::path invokation_path = getWritableDataDirectory();
std::filesystem::path vm_state_directory = invokation_path / "vm_state";
```

On Linux that is the **current directory**, captured at static-init
time. macOS gets a proper per-user location; Linux does not. Launch the
app from two directories and it uses two different `vm_state/`.

What is actually in there, and who reads it:

| file | read back? |
|---|---|
| `config.ini` | **never**. `src/config/config.cpp` contains no file I/O at all; the path is only `exists()`-checked and written (`utils.cpp:67`, `:489`). The processor, memory and cache settings in it are decorative - `cache_enabled=false` does not disable the cache, see 62. |
| `custom_pseudo_instructions.json` | **yes** - `custom_pseudo_manager.cpp:584`, `:607`, reached from `main_window.cpp:842` on every assemble. Custom pseudo-instructions therefore exist or not depending on where the app was started. |
| `temp.asm`, `disassembly.txt`, `errors_dump.json` | written then read back within the same operation |
| `registers_dump.json`, `vm_state_dump.json`, `memory_dump.json`, `cache_dump.json` | written only |

**Consequences seen in this work.** Every out-of-tree harness run from
the checkout wrote into the repository's own `vm_state/`; it is
gitignored, so no tracked file ever changed, but the runs were not
hermetic and `run_hermetic.sh` (which sets only the `XDG_*` variables)
did not isolate them. The harness runner now creates a sandbox
directory, copies `examples/` into it and `cd`s there before running, so
`vm_state/` is isolated too - proved by identical timestamps and an
identical aggregate md5 of the repository's `vm_state/` across a run.

Measured, for the record: an empty `vm_state/` and the repository's own
(which defines a custom pseudo-instruction `vlsi`) produce an identical
transcript. So the directory dependence is real but did **not** explain
the `b518` baseline - that was a missing `examples/ex_mem_fwd.s`, see
the note in 34.

**The durable fix** is to make `getWritableDataDirectory()` honour
`XDG_DATA_HOME` on Linux exactly as it already honours
`Application Support` on macOS - one function in `src/`, for a later
stage. Until then, launch directory is part of the app's configuration.

---

## 65. `kites-cli` segfaults before the REPL prompt, at `995d7ea` and at HEAD

Issue 56 recorded the CLI dying in **batch** mode on every program it
processes. It dies in **interactive** mode too, and earlier: with no
argument at all, before a single character of the prompt is printed.

| tree | `QT_QPA_PLATFORM=offscreen ./build/kites-cli` (no args) |
|---|---|
| `995d7ea`, the branch point | **SIGSEGV**, rc 139 |
| HEAD (`ae3c832`) | **SIGSEGV**, rc 139 |

Pre-existing, not a regression from this branch: `git diff --stat
995d7ea..HEAD -- cli/ CMakeLists.txt` is empty.

**Where.** `cli/main.cpp:51` constructs a `QCoreApplication`; the REPL
branch then constructs the processor at `cli/main.cpp:116`, before
`RunRepl` at `:138`. Backtrace under gdb, innermost first:

```
QGraphicsScenePrivate::init()                  <- SIGSEGV
Kites::CircuitScene::CircuitScene(QObject*)
Kites::RVSSCircuitScene::RVSSCircuitScene(QObject*)
std::make_unique<Kites::RVSSCircuitScene>()
Kites::RVSSProcessor::RVSSProcessor()
Kites::ProcessorFactory::RegisterVM<Kites::RVSSProcessor>(...)::{lambda()}
...
```

Every processor constructor builds a `CircuitScene`, which is a
`QGraphicsScene`, which needs a `QApplication` — a `QCoreApplication` is
not enough. This is the same fault as AGENTS.md "Harness Root Cause 1"
(`kites_test` crashing before it was given an offscreen `QApplication`)
and the same fault as issue 56, reached one step earlier. Issue 56 has
the mechanism in full: `DISABLE_GUI` is `PRIVATE` to `kites_cli`
(`CMakeLists.txt:135`) and never reaches `kites_core`, so the CLI links
a processor compiled with its GUI parts in.

**Consequence for this branch.** The REPL's direct `Run()` path — the
`reset` / `load` / `run` sequence at `command_handler.cpp:309`, `:277`,
which enters `Run()` with no `BeginRun()` — cannot be exercised through
the shipped binary at all. The bug issue 62 describes on that path is
real and is pinned by `ResetRace.DirectRunAfterResetStillExecutes` in
`kites_test`, which has a `QApplication`; it is simply unreachable from
`./build/kites-cli` today for this unrelated reason. Anyone verifying
the REPL by hand will meet this crash first.

Not fixed here: the fix is in `CMakeLists.txt`, which is outside this
branch's scope. See issue 56 for why an added `QApplication` would be
the wrong answer.

---

## 66. Only one of the four rv5s variants handles `ecall` at all

`handle_syscall()` is pure virtual on the base
(`rv5s_processor_base.h:104`) and all four variants override it. **Three
of the four overrides are never called.**

| variant | definition | called from |
|---|---|---|
| `RV5StageProcessorNHNF` | `rv5s_processor_nh_nf.cpp:332` | `rv5s_processor_nh_nf.cpp:204`, in `pipeline_execute()` |
| `RV5StageProcessorHF` | `rv5s_processor_h_f.cpp:461` | **nowhere** |
| `RV5StageProcessorHNF` | `rv5s_processor_h_nf.cpp:326` | **nowhere** |
| `RV5StageProcessorNHF` | `rv5s_processor_nh_f.cpp:625` | **nowhere** |

`grep -n handle_syscall src/processor/rv5s/*.cpp` returns exactly one
call site. In the other three files the SYSTEM opcode `0b1110011`
appears only inside the uncalled body, so their `pipeline_execute()` has
no branch for it and an `ecall` falls through to the ordinary ALU path:
it is decoded as an I-type with `funct3 == 0b000`, i.e. executed as an
`addi`-shaped instruction, and the pipeline continues to the next
instruction.

**`nh_nf` treats any SYSTEM instruction as exit, regardless of `a7`.**
The body at `rv5s_processor_nh_nf.cpp:332-341` tests only the opcode and
`funct3`; it never reads `a7`, so it does not distinguish
`SYSCALL_EXIT` (10) from `SYSCALL_READ` (63) or any other service, and
`ebreak` — same opcode, same `funct3`, differing only in the immediate —
takes the same path. Every one of them stops the processor and writes
`output_status_ = "ECALL_EXIT"`.

**Measured.** A short program ending in `li a7, 63` then `ecall` — `a7`
set to `SYSCALL_READ`, with nothing in the input queue — run on each
variant through an out-of-tree harness with a 6 s watchdog (breakpoints
cleared first, because `LoadProgram` auto-adds one at `program_size_`):

| variant | result |
|---|---|
| `rv5-nh-nf` | returns; the handler fires and the run stops at the `ecall` |
| `rv5-h-f`, `rv5-h-nf`, `rv5-nh-f` | return, `pc == 28`, program ran to the end |

**None of the four blocks.** This was worth measuring because
`SYSCALL_READ` is the one service that could park a run in an untimed
wait while a Stop is pending. It cannot here: the only
`input_cv_.wait()` in the tree is `rvss_processor.cpp:524`, and no rv5s
path reaches it. The only untimed waits the rv5s loops can enter are the
pause waits at `rv5s_processor_base.cpp:33` and `rvss_processor.cpp:136`,
both of which `StopAndReset` wakes.

Status: **recorded, not fixed.** Three dead overrides and an `a7`-blind
fourth are simulator-semantics work, not threading work, and AGENTS.md
rule 4 keeps core logic out of this branch.

---

## 67. Intentional behaviour change: an `ecall` exit no longer resets the processor

At `995d7ea`, `RV5StageProcessorNHNF::handle_syscall()` called
`RequestStop()`, and `RequestStop()` called `Reset()` — from inside
`Run()`, on the worker's own thread. A program that ended in `ecall`
therefore wiped its own registers, memory and undo history at the moment
it finished, destroying exactly the state the user ran it to see.

That is gone and is **not** being restored. `RequestStop()` now
dispatches on the caller: `StopFromRunLoop()`
(`src/processor/processor_base.cpp`) returns true when the caller *is*
the run thread (`worker_in_run_ && run_thread_ ==
QThread::currentThreadId()`), publishes the stop, wakes the pause wait,
and does nothing else. The processor is left stopped with its final
state intact.

Two reasons, not one. It is the behaviour a user wants — a finished
program should leave its results on screen — and it is the only safe
behaviour, because the old path reset the processor while the loop it
was called from was still running, which is the same UI-thread-versus-
worker collision issue 62 is about, with the two parties on one thread.

**Also a performance fix.** Between `995d7ea` and `3e44e05`,
`RequestStop()` waited for the worker to leave `Run()` before resetting.
Called *by* that worker, that wait can only time out. Measured, `run()`
wall time on a program ending in `ecall`:

| tree | `rv5-nh-nf` | the other three variants |
|---|---|---|
| `995d7ea` | 0 ms (and reset its own state) | 0 ms |
| HEAD before `3e44e05` | **3016 ms** per `ecall` | 0 ms |
| HEAD (`ae3c832`) | 0 ms | 0 ms |

The other three are unaffected because they never call `handle_syscall`
at all — see issue 66.

**The change is confined to the worker-thread caller.** The only other
`RequestStop()` caller is `ProcessorManager::step()`
(`processor_manager.cpp:285`), which fires when a program finishes while
being stepped. That one runs on the UI thread, so `StopFromRunLoop()`
declines it and it still goes through `StopAndReset()` — it still
resets, exactly as it did at `995d7ea`. Nothing in `src/ui/` or `cli/`
depended on the `ecall` path resetting; anything that comes to depend
on it must call `reset()` explicitly.
