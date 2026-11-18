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

**The missing step is one character**, found while surveying NaN
handling. `src/common/instructions.cpp`:

```cpp
static const std::unordered_set<std::string> FDExtensionR3TypeInstructions = {
    "fmv.x.w",
    "fmv.w.x",
    "fclass.s"        // <- no comma
    "fclass.d",       // so the two adjacent literals concatenate
    "fmv.x.d",
    "fmv.d.x",
};
```

C++ concatenates adjacent string literals, so the set contains
`"fclass.sfclass.d"` and **neither** real name.
`isValidFDR3TypeInstruction` therefore returns false for both,
`code_generator.cpp:439` falls through to its `else`, and the throw
names the instruction. `fence`/`fence_i` are a separate, genuine gap;
this half is a typo.

Not fixed here: every stage that has met it so far had the assembler out
of scope. It is a one-character change plus a test that assembles
`fclass.s` and `fclass.d`.

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

## 57. FIXED: `fcvt.s.d` and `fcvt.d.s` were never mapped to an ALU operation

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

### Fixed

In `bffe212`, with the tests in `3e23f78`. Both halves had to go, and the
entry above was right that mapping alone would not have been enough.

**The mapping.** Both control units gained funct7 `0b0100000` ->
`FCVT_S_D` and `0b0100001` -> `FCVT_D_S`. The entry named only
`rvss_control_unit.cpp`; `rv5s_control_unit.cpp`'s F-type decode (the
`alu_op_ == 3` switch) had exactly the same gap and returned
`AluOp::NONE` for the same two instructions, so rv5s was broken the same
way and is fixed the same way.

**The two ALU bodies.** `FCVT_D_S` now memcpy's a `float` out of the low
32 bits of `ina` instead of `static_cast<float>(ina)`, which converted
the bit pattern as an integer - 1.5f, `0x3FC00000`, became
1067450368.0. `FCVT_S_D` now rounds the memcpy'd `a` once and returns
the 32-bit pattern **zero-extended**, through `int_result`, because the
shared tail memcpy's eight bytes of `double` and would otherwise have
stored the double bits of the rounded single. Zero-extension rather than
NaN-boxing, because that is what `Alu::fpexecute` already returns and
reads; nothing in this simulator NaN-boxes.

Measured on `RVSSProcessor` and on all four rv5s variants, destination
preloaded with `0x40934800ABCDEF12`:

| program | before | after | correct |
|---|---|---|---|
| `fcvt.s.d` of 1.5 | **`0x0000000000000000`** | `0x3FC00000` | `0x3FC00000` |
| `fcvt.s.d` of 0.1 | **`0x0`** | `0x3DCCCCCD` | `0x3DCCCCCD` |
| `fcvt.s.d` of 1+2^-24 (a tie) | **`0x0`** | `0x3F800000` | `0x3F800000`, half to even |
| `fcvt.s.d` of 1+3*2^-24 (a tie) | **`0x0`** | `0x3F800002` | `0x3F800002`, half to even |
| `fcvt.s.d` of 1e300 / -1e300 | **`0x0`** | `0x7F800000` / `0xFF800000` | overflow to +/-inf |
| `fcvt.s.d` of 1e-40 | **`0x0`** | `0x000116C2` | a subnormal single |
| `fcvt.s.d` of -0.0 | **`0x0`** | `0x80000000` | the sign survives |
| `fcvt.d.s` of 1.5f | **`0x0`** | `0x3FF8000000000000` | `0x3FF8000000000000` |
| `fcvt.d.s` of 0.1f | **`0x0`** | `0x3FB99999A0000000` | exact widening |
| `fcvt.d.s` of `0x00000001` | **`0x0`** | `0x36A0000000000000` | the smallest subnormal |
| `fcvt.d.s` of -0.0f | **`0x0`** | `0x8000000000000000` | the sign survives |

Every "correct" column was computed in Python with `struct.pack`, outside
the code under test. NaN rows are not in the table because IEEE 754 does
not pin a converted NaN's payload; the tests assert quiet-NaN-ness, and
what this host actually produces is `0x7FF8000000000000` ->
`0x7FC00000` and `0x7FC00000` -> `0x7FF8000000000000`.

The caveats are off both entries, in `f22bc25`.

---

## 58. FIXED: float literals lost precision, the locale, and sometimes threw

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

### Re-measured, and it was worse than this

Fixed in `8e1dbc7`, with the tests in `f3666e8`. Re-measuring first, on
GCC 16 / Fedora 44, through `assemble()` and out of
`AssembledProgram::data_buffer`, found three things the entry above does
not say. Every "correct" column below was computed outside the code under
test, in Python, two ways that agree: `struct.pack`, and an exact
`Fraction`/`Decimal` round-half-to-even converter.

**1. Small magnitudes did not lose precision — they became ZERO.**
`std::to_string(double)` is `%f`: six digits after the point, and nothing
else.

| literal | before | after | correct |
|---|---|---|---|
| `.double 1e-7` | **`0x0000000000000000`** | `0x3E7AD7F29ABCAF48` | `0x3E7AD7F29ABCAF48` |
| `.double 1e-30` | **`0x0000000000000000`** | `0x39B4484BFEEBC2A0` | `0x39B4484BFEEBC2A0` |
| `.float 1e-7` | **`0x00000000`** | `0x33D6BF95` | `0x33D6BF95` |
| `.double 3.14159265358979` | `0x400921FB82C2BD7F` | `0x400921FB54442D11` | `0x400921FB54442D11` |
| `.double 2.718281828459045` | `0x4005BF0AA21A719B` | `0x4005BF0A8B145769` | `0x4005BF0A8B145769` |
| `.double 123456789.123456789` | `0x419D6F34547E6B83` | `0x419D6F34547E6B75` | `0x419D6F34547E6B75` |
| `.float 3.14159265358979` | `0x40490FDC` | `0x40490FDB` | `0x40490FDB` |
| `.double 0.1`, `0.5`, `1e0`, `-0.0` | exact | exact | exact |

Note the last row against the original table above: on this toolchain
`.double 0.1` was already exact, because `"0.100000"` and `"0.1"` are the
same decimal. The `0x3FB999AA60913A50` recorded above is the double
nearest `0.100001`, so that row measured a literal with a 1 in it, not
`0.1`. The shape of the finding stands; that one row did not.

**2. It followed the C locale, and the app changes the C locale.**
Nothing in `src/`, `cli/` or `test/` calls `setlocale` — but
**`QApplication`'s constructor does**, `setlocale(LC_ALL, "")`. Measured
across the constructor: `LC_NUMERIC` goes `C` → the user's, and
`std::to_string(0.1)` goes `0.100000` → `0,100000`. `std::stod` then
stops at the comma.

| under `LC_ALL=de_DE.utf8` | before | after |
|---|---|---|
| `.double 0.1` | **`0x0000000000000000`** (0.0) | `0x3FB999999999999A` |
| `.double 3.14159265358979` | **`0x4008000000000000`** (3.0) | `0x400921FB54442D11` |
| `.float 0.1` | **`0x00000000`** | `0x3DCCCCCD` |

So a German- or French-locale user writing `.double 0.1` got zero. Pinned
by `FloatLiteral.ParsingDoesNotDependOnTheCLocale`, which sets
`LC_NUMERIC` itself and skips if `de_DE.utf8` is not installed.

**3. Some valid literals threw, out of the lexer and out of
`assemble()`.** `std::stod` throws `std::out_of_range` on a subnormal
result as well as on overflow. `main_window.cpp` catches it and shows a
`QMessageBox` whose entire text is **"stod"** — no line, no column.

| literal | before | after |
|---|---|---|
| `.double 5e-324` (a valid subnormal) | **threw `std::out_of_range("stod")`** | `0x0000000000000001` |
| `.double 1e-320` (a valid subnormal) | **threw** | `0x00000000000007E8` |
| `.float 1e300` | **threw `std::out_of_range("stof")`** | a located parse error |
| `.double 1e400`, `.double 1e-400` | **threw** | a located parse error |

The error is now an ordinary diagnostic, the same shape the `.zero`
directive already used:

```json
{ "line": 2, "column": 16, "endColumn": 22, "severity": "error",
  "category": "Assembler",
  "message": "Float literal '1e-400' is out of range for a 64-bit double" }
```

### The fix

`lexer.cpp` passes the literal's TEXT through in the token instead of
`std::to_string(std::stod(value))`, and `parser.cpp` converts it once,
where it is used, with **`std::from_chars`** — locale-independent by
specification, correctly rounded, and it *reports* an out-of-range value
rather than throwing. `.float` converts from the text rather than
narrowing an already-rounded double, because two roundings can land one
ulp away: measured on `6748.838134765625000000000000001`, `0x45D2E6B5`
correctly rounded against `0x45D2E6B4` via a double. Integer literals are
untouched.

Six tests in `test/float_literal_test.cpp`; five were red before the fix.
Four controls, each reverting one hunk, each red again and named in the
stage report.

### Still rejected, deliberately not touched here

Two literal shapes never reach the float path at all, so neither is a
precision bug, and both are left alone:

| literal | lexed as | why |
|---|---|---|
| `.5` | `DIRECTIVE('')` `NUM('5')` | the directive scanner claims a leading `.` before the number scanner sees it, even though `float_regex` would accept `[0-9]*\.[0-9]+` |
| `1.` | `INVALID` | `float_regex` requires at least one digit after the point |
| `inf`, `nan`, `-inf`, `NaN` | `INVALID` | no numeric regex admits them; there is no spelling for a non-finite literal |

Accepting `.5` means changing directive lexing and accepting `1.` means
changing the regex; both deserve their own change and their own controls.

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

## 60. FIXED: `fcvt.d.lu` wrote zero, because the ALU case was labelled `FCVT_S_LU`

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

### Fixed

In `981ebf7`, with the tests in `3e23f78`: the label, one word. The
diagnosis above was exactly right and the body needed no change.

Measured on `RVSSProcessor` and on all four rv5s variants. The last two
rows are the ones that distinguish `fcvt.d.lu` from `fcvt.d.l`, and they
are the reason an unsigned conversion needs its own case at all:

| source | before | after | correct | what `fcvt.d.l` gives |
|---|---|---|---|---|
| 0 | `0x0` | `0x0000000000000000` | `0x0000000000000000` | same |
| 1 | **`0x0`** | `0x3FF0000000000000` | `0x3FF0000000000000` | same |
| 965 | **`0x0`** | `0x408E280000000000` | `0x408E280000000000` | same |
| 2^53+1 | **`0x0`** | `0x4340000000000000` | rounds to 2^53 | same |
| **2^63** | **`0x0`** | `0x43E0000000000000` | +9.22e18 | `0xC3E0000000000000`, **negative** |
| **2^64-1** | **`0x0`** | `0x43F0000000000000` | +1.84e19 | `0xBFF0000000000000`, **-1.0** |

The caveat is off the entry, in `f22bc25`.


---

## 61. FIXED: the eight fused multiply-adds never wrote their destination

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

### Fixed

In `07c756a`, with the tests in `3e23f78`. The entry above got both known
halves right, and there was a third nobody had looked for.

**1. The predicates.** `isFInstruction` and `isDInstruction` now cover
the four R4-type opcodes and classify by **fmt** (bits 26:25, `00`
single, `01` double) - because an R4-type's bits 31:25 are `rs3 << 2 |
fmt`, not a funct7. On rv5s that predicate turned out to be the single
gate on everything else: `rv5s_processor_base.cpp` decodes `frs3`, reads
`freg3_data`, sets `freg_write`, `rv5s_hdu.cpp` stalls on `frs3` and the
forwarding variants forward to it - all of it already written, all of it
behind that one `if`.

**2. The ALU-op selection, rvss only.** The unconditional
`case 0b1000011: return FMADD_S;` under its "TODO: correct this" is gone,
replaced by four top-level opcode cases that select on `funct2`. The
four fmt-aware blocks that had been sitting unreachable as funct7 cases
nested inside opcode `0b1010011` - 60 lines of correct, dead code - are
deleted.

**3. A third hole, in writeback, which only bites once the first two are
fixed.** `WriteBackFloat`/`WriteBackDouble` in **both** models chose
GPR-versus-FPR from funct7 alone:

```cpp
if (funct7 == 0b1010000 || funct7 == 0b1100000 || funct7 == 0b1110000)  // -> GPR
```

For an FMA those bits are `rs3 << 2 | fmt`. `fmadd.s fd, f1, f2, f20`
reads `0b1010000` - the pattern that means `feq.s`/`fcvt.w.s` - so the
result would have gone to the **integer** register `x[rd]`; `rs3` = 24
and 28 collide the same way, and `.d` collides with the `...0001` row.
Routing the eight without this would have turned "writes nothing" into
"corrupts a GPR", which is worse. All four sites now require opcode
`0b1010011`, and
`FloatCompleteness.FusedMultiplyAddDoesNotTouchIntegerRegisters` covers
the three colliding `rs3` values in both widths.

Measured on `RVSSProcessor` and on all four rv5s variants, destination
preloaded with `0x40934800ABCDEF12`:

| program | before | after | correct |
|---|---|---|---|
| `fmadd.d` 3,5,7 | **the sentinel** | `0x4036000000000000` | 22.0 |
| `fmsub.d` 3,5,7 | **the sentinel** | `0x4020000000000000` | 8.0 |
| `fnmsub.d` 3,5,7 | **the sentinel** | `0xC020000000000000` | -8.0 |
| `fnmadd.d` 3,5,7 | **the sentinel** | `0xC036000000000000` | -22.0 |
| `fmadd.s` 3,5,7 | **the sentinel** | `0x41B00000` | 22.0f |
| `fmsub.s` / `fnmsub.s` / `fnmadd.s` | **the sentinel** | `0x41000000` / `0xC1000000` / `0xC1B00000` | 8, -8, -22 |
| `fmadd.d(-1.0, +0.0, -0.0)` | **the sentinel** | `0x8000000000000000` | -0.0 |

And the case that makes a fused multiply-add worth having at all, with
`a = 1+2^-52`, `b = 1-2^-52`, `c = -1.0`, so that `a*b` is exactly
`1 - 2^-104`:

| | result |
|---|---|
| before | the sentinel, nothing written |
| **fused, one rounding (correct)** | **`0xB970000000000000`**, -4.93e-32 |
| multiplied then added, two roundings | `0x0000000000000000` |

`fmadd.d(inf, 0, 1)` is an invalid operation; the test asserts
quiet-NaN-ness rather than a payload, and this host produces
`0xFFF8000000000000`.

The caveats are off all eight entries, in `f22bc25`.


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

## 66. FIXED: only one of the four rv5s variants handled `ecall` at all

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

Status at the time: **recorded, not fixed.** Three dead overrides and
an `a7`-blind fourth are simulator-semantics work, not threading work, and
AGENTS.md rule 4 kept core logic out of that branch.

### Answered from the code before the fix (base `19cf41f`)

- **Which stage reads a0/a7.** None reads a7. nh_nf acted on the opcode
  alone, in EX (`rv5s_processor_nh_nf.cpp:210-214`). The other three
  never reached their overrides.
- **Forwarded or stalled?** Neither, on any variant. An ecall encodes
  rs1 = rs2 = 0, so the hazard unit (`rv5s_hdu.cpp:39-57`) and the
  forwarding units never learn that it reads a0-a2 and a7.
- **Does an exit drain?** No. nh_nf returned from `pipeline_execute`
  before EX/MEM was latched, and the run stopped after that Step. The
  instruction just before the ecall had done MEM but not WB, so its
  register write was lost. A store just before the ecall survived (its
  MEM had run). Measured with the `lirepro` harness on
  `li a7,10; nop x4; li x5,5; ecall; li x6,9`, run through `DebugRun`:

  | | rv5-nh-nf | rv5-nh-f | rv5-h-nf | rv5-h-f |
  |---|---|---|---|---|
  | base, x5 / x6 | **0 / 0** (write lost) | 5 / **9** (no exit) | 5 / **9** | 5 / **9** |
  | after, x5 / x6 | 5 / 0 | 5 / 0 | 5 / 0 | 5 / 0 |

- **The worker-thread stop (62).** On the Run worker, `RequestStop` →
  `StopFromRunLoop` was correct. Stepping onto the ecall was not: it
  self-deadlocked. See 102.
- **rvss's own exit** called `exit(0)`. See 101.

### The fix (`e2eeacf`, `03e4c9d`, `24f763b`)

The design choice (the user's): the pipelines serve an ecall at
**writeback**, through the same service rvss uses.

- `RVSSProcessor::HandleSyscall` moved verbatim into
  `ProcessorBase::ServeSyscall(StepDelta &)` (`processor_base.cpp:528`).
  rvss calls it from Execute (`rvss_processor.cpp:419-421`), so the five
  models share one table: 1 print int, 2 print float, 3 print double,
  4 print string, 10 exit, 63 read (fd 0, with the input wait), 64 write
  (fd 1). Anything else prints "Unknown syscall number" on stderr and
  carries on (`:742`). 93 stays unknown.
- `RV5StageVM_Base::pipeline_writeback` serves an instruction with
  opcode `0x73` and funct3 0 (`rv5s_processor_base.cpp:682`), via
  `serve_syscall_at_writeback` (`:757`):
  1. Every older instruction has retired, so a0-a2 and a7 are
     architectural on every variant. No forwarding path and no stall was
     added for them.
  2. An exit drains by construction.
  3. Everything younger is flushed (IF/ID, ID/EX, EX/MEM, any
     `stall_cycles_`) and refetched from ecall + 4, as for a trap. This
     happens before MEM and EX run in that Step, so a store right after
     the exit never writes.
  4. The cost is three bubbles per ecall.
- The four `handle_syscall` overrides and their pure virtual are gone.
  nh_nf's ecall early-return is gone, so its ecall now flows down the
  pipe as a no-op like on the other three (`rv5s_processor_nh_nf.cpp:216`).
- `ebreak` (funct3 0, imm 1) is served exactly like `ecall` on all five,
  because that is the test rvss always used. Not changed here.
- Residue, not undone: a younger FP instruction that was in EX in the
  cycle before the flush has already written fcsr (`WriteCsr(0x003)`;
  see 72).

**Tests.** `test/ecall_syscall_test.cpp`, value-parameterized per model
(one ctest process per model and case, 40 tests). Each case runs
unpadded and padded. rvss, h-nf and h-f are asserted in both forms;
nh-nf and nh-f are asserted padded only, as their contract. The cases:
- print an int;
- read "42" and echo it;
- the same, with the input arriving 300 ms later;
- Stop while parked in the read;
- `li a7,93` immediately before an ecall, after an earlier `li a7,10`;
- a store right before the exit, and one right after it;
- an ALU write right before the exit;
- stepping onto the exit.

An `atexit` guard turns an `exit()` during a run into status 1. Without
it, the old rvss `exit(0)` made ctest report those tests as passed.

**Measured, not asserted:** with every case asserted unpadded on all
five, only `ReadsAnIntegerAndEchoesIt/rv5-nh-nf` fails. Its ordinary
`lbu`→`addi` and `mul`→`add` pairs have no forwarding. The ecall itself
is right unpadded on both no-hazard variants, because it reads at
writeback.

**Red** (base + tests only): 40 of 44 new tests fail, including all 40
ecall rows except `StopWakesARunWaitingForInput/rvss`; rvss's read wait
was already stop-aware. The nh-nf stepping row fails by its 10 s
watchdog.

**Controls** (out of tree, each reverted alone):
- `exit(0)` back → 35 of 40 red; only the five Stop-while-waiting rows
  pass, because they never exit.
- Exit via `RequestStop()` → exactly the five
  `SteppingOntoAnExitEndsTheProgramWithoutHanging` rows red, each at the
  10 s watchdog.
- The writeback service call removed → all 32 rv5s rows red, the 8 rvss
  rows green.
- The flush removed → `ReadsAnIntegerAndEchoesIt` and
  `ExitKeepsAStoreJustBeforeItAndDropsOneAfter`, on rv5-h-nf and rv5-h-f.

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

**Update (66, 101, 102).** `handle_syscall` no longer exists. The exit
service is now `ProcessorBase::ServeSyscall`, on all five models. It
stops through `StopForProgramExit()` (`processor_base.cpp:749`), which is
`StopFromRunLoop()` on the run thread, exactly as described above. On a
manual step it does not call `RequestStop()` at all (102):
`StepAndNotify()` returns false, and `ProcessorManager::step()` then
takes the end-of-program path this entry describes, reset included.

**The change is confined to the worker-thread caller.** The only other
`RequestStop()` caller is `ProcessorManager::step()`
(`processor_manager.cpp:285`), which fires when a program finishes while
being stepped. That one runs on the UI thread, so `StopFromRunLoop()`
declines it and it still goes through `StopAndReset()` — it still
resets, exactly as it did at `995d7ea`. Nothing in `src/ui/` or `cli/`
depended on the `ecall` path resetting; anything that comes to depend
on it must call `reset()` explicitly.

---

## 68. FIXED: a glob scanned the whole filesystem on every build (and not from the file this entry first blamed)

Fixed in `52e202a`, one line. **The first version of this entry named the
wrong file**, and the correction is the interesting part, so both are kept.

### What it blamed, and why that was wrong

`test/CMakeLists.txt:7` globs a relative, unanchored pattern:

```cmake
file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS "*.cpp")
```

That *looks* like the culprit, and this entry said it was. It is not.
CMake resolves a relative pattern against the current source directory in
the `CONFIGURE_DEPENDS` verification step as well as in the glob itself,
and it was always recorded correctly, as
`.../Atlas/test/*.cpp`. Measured: that pattern is still in
`VerifyGlobs.cmake` after the fix, unchanged and absolute.

### What it actually was

`CMakeLists.txt:47`, in the **root** file:

```cmake
file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS "${TEST_DIR}/*.cpp")
```

`SRC_DIR` and `EXTERNAL_DIR` are set at `CMakeLists.txt:25-26`.
**`TEST_DIR` was never set anywhere in the project** — `grep -n TEST_DIR`
over every `CMakeLists.txt` and `*.cmake` returned that one line and
nothing else. So the pattern expanded to `"/*.cpp"`, an absolute path
rooted at `/`, and CMake recorded it in both build directories:

```cmake
# build-tests/CMakeFiles/VerifyGlobs.cmake:64   (before)
file(GLOB_RECURSE NEW_GLOB LIST_DIRECTORIES false "/*.cpp")
```

It is in `build/CMakeFiles/VerifyGlobs.cmake` too, at the same line, which
is the tell: `build/` is configured without `ENABLE_TESTS` and never reads
`test/CMakeLists.txt` at all.

The variable it defines is **dead**: the only readers of `TEST_FILES` are
`test/CMakeLists.txt:10` and `:56`, and line 7 of that file has already
re-set it in the subdirectory scope before either is reached. So the
project walked the filesystem twice per build to compute a list nobody
read.

### Measured, before and after

| | before | after |
|---|---|---|
| no-op `cmake --build build-tests`, best of 3 | **7.41 s** | **0.06 s** |
| `-- Configuring done` | 7.8 s | 0.5 s |
| a `.cpp` created under `/tmp`, outside the repo | `GLOB mismatch!`, `Re-running CMake...`, **22.93 s** | no mismatch, no re-configure, 0.06 s |
| `/*.cpp` in `VerifyGlobs.cmake` | line 64 of both build dirs | **gone**; all five recorded globs are absolute paths inside the checkout |

The stray-file reproduction, verbatim:

```
[0/2] Re-checking globbed directories...
-- GLOB mismatch!
  +/tmp/.../scratchpad/p11/stray_glob_probe.cpp
[1/2] Re-running CMake...
-- Configuring done (7.8s)
```

### The fix, and what it did not change

One line beside the other two: `set(TEST_DIR "test")`. Deleting the dead
glob would work equally well and is the obvious follow-up; defining the
variable keeps the root file's evident intent and is the smaller change.

`kites_test` still compiles **exactly the same 51 objects** — the 50
`test/*.cpp` plus `kites_test_autogen/mocs_compilation.cpp` — diffed
before and after. `CONFIGURE_DEPENDS` still works, checked both ways: a
new `test/zz_glob_probe_test.cpp` was picked up by a plain
`cmake --build`, with no manual cmake run, and registered as test #608;
deleting it took the count back to 607. Suite 607/607, reconciled by name
against the base: no test added, none removed.

As the first version of this entry said and still holds: the bad glob
never *compiled* anything foreign. The cost was a filesystem walk per
build and a spurious re-configure, not a correctness problem.

---

## 69. `assemble(std::istream&)` reports every parse error as "Could not open the file."

The in-memory overload (`src/assembler/assembler.cpp:104`) exists so the
editor and tests can assemble without touching disk. It lexes and parses
correctly — but the moment the parser records a *second-tier* error, the
real diagnostic is replaced by an I/O message.

**Mechanism.** Every `errors::*` constructor is handed
`GetLineFromFile(filename_, line)` so the error can quote the offending
line. The stream overload has no file: `lexer->getFilename()` is a
placeholder, `GetLineFromFile` cannot open it, and it throws. The throw
escapes `assemble()` in place of the diagnostic the parser had just
recorded.

**Measured**, assembling one directive per run through the stream
overload. Every one of these is a different mistake, and every one
arrives as the same message:

| source | what comes out |
|---|---|
| `.double +1.5` | `Could not open the file.: iostream error` |
| `.double 1.` | `Could not open the file.: iostream error` |
| `.word 1.5` | `Could not open the file.: iostream error` |
| `.float 1e400` | `Could not open the file.: iostream error` |
| `.double nan` | `Could not open the file.: iostream error` |

**The file-path overload is unaffected** (`assemble(const std::string&)`,
`assembler.cpp:60`): it has a real file, `GetLineFromFile` reads it, and
the diagnostic survives with its line, column and category. The same
`.float 1e-400` that produces the message above produces this through the
file path:

```json
{ "line": 2, "column": 16, "endColumn": 22, "severity": "error",
  "category": "Assembler",
  "message": "Float literal '1e-400' is out of range for a 64-bit double" }
```

Who is exposed: nothing in the shipped app today. `MainWindow` writes the
editor text to `temporary_assembly_file_path` and calls the file overload
(`main_window.cpp:867`), and `getDiagnostics()` is the commented-out stub
of AGENTS.md known issue 3. It bites tests and harnesses that assemble
from a stream, which is why `test/float_literal_test.cpp` uses the file
overload throughout.

The fix is to stop making the error text depend on re-reading the source:
either pass the line text the lexer already has, or let `GetLineFromFile`
return an empty string instead of throwing when the file cannot be
opened. Not done here — it touches every error construction site in the
parser.

---

## 70. FIXED: a NaN result carried the input's payload and sign

RISC-V's F extension, "NaN Generation and Propagation": unless an
instruction says otherwise, a NaN **result** is the canonical NaN —
positive, quiet bit set, payload zero. `0x7FC00000` single,
`0x7FF8000000000000` double. The payload of an input NaN is not
propagated, and neither is its sign. This simulator propagated both.

Fixed in `ad36904`, with the tests in `45a2871`.

**Surveyed before changing anything**: 306 cases x 5 models = 1530 runs,
four NaN shapes per width, through assembled programs on real
processors. All five models agreed on every case but one (see 74).

| `fadd.s`, NaN operand | before | after | spec |
|---|---|---|---|
| `0x7FC00000` canonical qNaN | `0x7FC00000` | `0x7FC00000` | `0x7FC00000` |
| `0xFFC00000` negative qNaN | **`0xFFC00000`** | `0x7FC00000` | `0x7FC00000` |
| `0x7FE00000` high-payload qNaN | **`0x7FE00000`** | `0x7FC00000` | `0x7FC00000` |
| `0x7F800001` sNaN | **`0x7FC00001`** | `0x7FC00000` | `0x7FC00000` |

The same four rows for `fsub`, `fmul`, `fdiv`, `fsqrt`, all eight fused
multiply-adds, `fcvt.s.d` and `fcvt.d.s`, in both widths. The invalid
operations returned whatever the host produces, which for x86-64 is a
**negative** default NaN:

| | before | after |
|---|---|---|
| `inf - inf` | **`0xFFC00000`** / `0xFFF8000000000000` | canonical |
| `0 * inf` | **`0xFFC00000`** / `0xFFF8000000000000` | canonical |
| `inf / inf` | **`0xFFC00000`** / `0xFFF8000000000000` | canonical |
| `fma(inf, 0, 1)` | **`0xFFC00000`** / `0xFFF8000000000000` | canonical |
| `0 / 0`, `sqrt(-1)` | `0x7FC00000` / `0x7FF8…` — positive, by accident | canonical |

`fmin`/`fmax` needed nothing extra. Their rule is different — one NaN
operand returns the *other operand*, not a NaN — and the survey found
that half already correct in 32 of 32 cases. Only the both-NaN case
returns a NaN, and it is now canonical.

### Where, and why an allow-list

Every NaN result leaves through one of two exits of
`Alu::fpexecute`/`Alu::dfpexecute`, which the survey confirmed by
enumerating every `return` in both functions:

* the shared tail's float packing — all the arithmetic, `fmin`/`fmax`,
  the FMAs, `fcvt.d.s`, the int-to-float conversions, and `fsgnj*`;
* the tail's `is_int_result` return — `feq`/`flt`/`fle`, and `fcvt.s.d`,
  which goes that way because its result is a 32-bit single;
* everything else returns early and never reaches either: the
  float-to-int conversions, `fclass`, and the four `fmv` forms.
  `flw`/`fsw`/`fld`/`fsd` are not ALU ops at all.

The fix canonicalizes at those two points, driven by an **allow-list of
the 24 producers** rather than a list of exemptions. Counted from the
switch labels, the two functions carry 60 between them — 29 in
`fpexecute`, 31 in `dfpexecute`:

| exit | labels |
|---|---|
| the shared tail's float packing | 37 |
| the tail's `is_int_result` return | 7 |
| an early return, reaching neither point | 16 |

So **44 reach the tail, 24 canonicalize** — 23 at the float exit plus
`FCVT_S_D` at the `is_int_result` exit — and 20 do not: at the float
exit the six `fsgnj`/`fsgnjn`/`fsgnjx`, which must keep an operand's
payload and sign, and the eight integer-to-float conversions, which
cannot produce a NaN; at the `is_int_result` exit the six
`feq`/`flt`/`fle`, whose results are integers. The 16 early returns are
the eight `fcvt.w/wu/l/lu`, the two `fclass`, the four `fmv` forms, and
the two `ADD` labels the address path reuses.

Naming the producers means an op added to the tail later canonicalizes
nothing until someone decides it should; the other way round it would
silently start rewriting results. The `is_int_result` point tests for
`FCVT_S_D` by name, so `feq`/`flt`/`fle` cannot be caught by it.

*(An earlier version of this entry, of the commit message for `ad36904`
and of the stage report said "40 reach the tail, 26 canonicalize, 14 do
not". The 14 was right, for the float exit; the other two were a
miscount. The list in the code has always held these same 24 ops —
nothing about the behaviour changed, only the arithmetic describing
it.)*

A canonical single is returned in the low 32 bits, zero-extended — this
simulator's convention for every single, not the spec's. See 71.

---

## 71. A single is zero-extended in an f register, not NaN-boxed

RISC-V requires a 32-bit value held in a 64-bit `f` register to be
**NaN-boxed**: the upper 32 bits all ones, so that a single looks like a
qNaN to any double-precision operation that reads it. This simulator
zero-extends instead, everywhere and consistently.

Measured, and consistent in all five models: `fmv.w.x` of `0x3F800000`
leaves `0x000000003F800000`; `Alu::fpexecute` returns
`static_cast<uint64_t>(result_bits)` from a 32-bit pattern; every reader
takes the low 32 bits with `memcpy(&a, &ina, sizeof(float))`.

**It is self-consistent, which is why nothing has broken.** A
single-precision value written by one instruction is read correctly by
the next, because both ends agree. The deviation shows only against real
hardware and against a program that reads a single-width register with a
double-width instruction: on hardware that yields a NaN, here it yields
a small denormal double.

Touch points a fix would need, in order:

1. `Alu::fpexecute`'s single return — OR in `0xFFFFFFFF00000000`;
2. the `FMV_W_X` case and the `FCVT_S_D` path, which both hand back a
   32-bit pattern;
3. `flw` in the memory stage of rvss and of `rv5s_processor_base.cpp`
   (see 74, which has to be settled first);
4. every reader: the `memcpy(&a, &ina, sizeof(float))` sites are
   unaffected, but anything comparing a whole 64-bit register against a
   single's bits is not — including `test/float_test_support.h` and the
   float tests, which assert the zero-extended form today.

Deliberately not done as part of 70: canonical NaN is about the *value*
of a NaN result, boxing is about the *representation* of every single.
Mixing them would make both unreviewable.

---

## 72. `fflags` is never written, and the flags it would hold are not RISC-V's

Four independent defects, all measured over the 1530 survey runs.

**1. `fflags` (CSR 0x001) is never written by anything.** It read `0` in
every one of those runs. `RegisterFile` keeps CSRs in a flat array
indexed by address (`registers.cpp:66-78`), so 0x001 `fflags`, 0x002
`frm` and 0x003 `fcsr` are three unrelated slots with no aliasing;
RISC-V defines `fflags` as `fcsr[4:0]`. The ALU writes **0x003** only
(`rvss_processor.cpp:358` and the rv5s equivalents), so a program that
reads `fflags` — the normal way to read the flags — gets zero.

**2. The bit order is reversed.** `alu.h:23-27` defines
`FCSR_INVALID_OP` as bit 0 … `FCSR_INEXACT` as bit 4. RISC-V's `fflags`
is NX bit 0, UF 1, OF 2, DZ 3, NV 4. Every flag is in the wrong place.

**3. Every double-precision op's status is narrowed to a bool.**
`Alu::dfpexecute` returns `std::pair<uint64_t, bool>` (`alu.h:376`)
while `fpexecute` returns `std::pair<uint64_t, uint8_t>`. The measured
`fcsr` values across the whole survey were `0`, `1` and `2` and nothing
else — doubles can only ever report "some flag" or "none".

**4. The write replaces rather than accrues.** `WriteCsr(0x003, …)`
stores the new value, so each instruction clears whatever the previous
one raised. RISC-V's flags are sticky until explicitly cleared.

A fix touches `alu.h`, `alu.cpp`, `rvss_processor.cpp` and four rv5s
variants, and it needs its own survey: the flag *values* the ALU
computes have never been checked against the spec either, only their
plumbing. That is why the canonical-NaN tests assert no flags at all —
asserting today's behaviour would pin an encoding that has to change.

---

## 73. `fcvt.w.s` saturates the wrong way for NaN, and has a boundary UB

The float-to-int conversions guard before converting —
`!isfinite(a) || a > max || a < min` — and saturate explicitly, so no
out-of-range `static_cast` runs on ordinary inputs. Seven of the eight
pick the saturation value with `(a < 0) ? min : max`, which sends a NaN
to the maximum, as the spec's table requires. `FCVT_W_S` alone
(`src/processor/alu.cpp`) uses `(a > 0 ? INT32_MAX : INT32_MIN)` — and
`NaN > 0` is false.

| | measured, all five models | spec |
|---|---|---|
| `fcvt.w.s` of a qNaN | **`0xFFFFFFFF80000000`** | `0x000000007FFFFFFF` |
| `fcvt.w.d`, `fcvt.l.s/d`, `fcvt.lu.s/d`, `fcvt.wu.s/d` of a qNaN | max, or 0 for unsigned | correct |

**A second, separate defect at the boundary.** `INT32_MAX` is 2147483647,
which as a `float` rounds **up** to 2147483648.0f. So for exactly that
input, `a > static_cast<float>(INT32_MAX)` is false, the guard does not
fire, and the else branch runs `static_cast<int32_t>(2147483648.0f)` —
out of range, and **undefined behaviour** in C++:

| input | measured here | spec |
|---|---|---|
| `0x4EFFFFFF` = 2147483520.0f, the largest in range | `0x7FFFFF80` | correct |
| `0x4F000000` = 2147483648.0f, one ulp past | **`0xFFFFFFFF80000000`** | `0x000000007FFFFFFF` |

The measured value is x86-64's "integer indefinite", which is what
`cvttss2si` returns on overflow. Being UB, it is not guaranteed to stay
that at another optimisation level or on another target. The same
rounding argument applies to `(float)INT64_MAX` and to the `.d` forms
against `INT64_MAX`; only the `.s`/`w` pair was measured.

Recorded, not fixed: this stage was canonical NaN, and these are
integer-result paths that leave through early returns and were never in
its allow-list.

---

## 74. FIXED: rv5s sign-extended a loaded single; rvss zero-extends it

Found by the pass-through negative control of the NaN survey, and it is
the only case in 306 where the five models disagreed.

```
fsw f1, 0(x10)      # f1 = 0xFFC00000, a negative qNaN
flw f2, 0(x10)
```

| model | `f2` |
|---|---|
| rvss | `0x00000000FFC00000` |
| rv5-nh-nf, rv5-nh-f, rv5-h-nf, rv5-h-f | **`0xFFFFFFFFFFC00000`** |

rvss zero-extends the loaded word into the f register; all four rv5s
variants sign-extend it. Neither matches RISC-V, which NaN-boxes (71),
but **they do not match each other**, and that is the part that makes a
program's result depend on which model a student picked. Any single
whose bit 31 is set is affected — every negative single, not just NaNs.

Not fixed at the time: the fix is in the rv5s memory stage, outside
that stage's file, and it interacts with 71.

**Fixed (`7de4932`)** to match rvss and the convention 71 records
(zero-extended, not NaN-boxed). The FLW cast in `pipeline_memory` went
from `int32_t` to `uint32_t` (`rv5s_processor_base.cpp:568`; it was
`:560` at base). NaN-boxing stays open under 71.

`test/flw_zero_extend_test.cpp` runs `sw` of the word, then `flw f2`,
`fsd f2`, `ld x6`, padded, on all five models. Expected values come from
Python `struct`:

| word | f2 / x6 before, rv5s | after, all five |
|---|---|---|
| `0xBF800000` (-1.0) | `0xFFFFFFFFBF800000` | `0x00000000BF800000` |
| `0xC0200000` (-2.5) | `0xFFFFFFFFC0200000` | `0x00000000C0200000` |
| `0xFFC00000` (negative qNaN) | `0xFFFFFFFFFFC00000` | `0x00000000FFC00000` |

`fmv.x.w` is unchanged and pinned. It sign-extends into the x register
by the architecture (`alu.cpp:698-703`): `0xFFFFFFFFBF800000`, both
after `fmv.w.x` and after `flw`.

Red at base: `FlwZeroExtend.LoadedSingleIsZeroExtendedOnEveryModel`
fails and the `fmv.x.w` pin passes. The control (cast back to `int32_t`)
fails the same one test.

---

## 75. `fdiv` by zero returns a NaN, not an infinity

`AluOp::FDIV_S` and `FDIV_D` in `src/processor/alu.cpp` test the divisor
and return `quiet_NaN()` for **any** zero divisor:

```cpp
if (b == 0.0f)
{
    result = std::numeric_limits<float>::quiet_NaN();
    fcsr |= FCSR_DIV_BY_ZERO;
}
```

IEEE 754 and RISC-V both say a finite non-zero divided by zero is
**±inf**, with the divide-by-zero flag raised; only `0/0` is a NaN, and
that one raises invalid rather than divide-by-zero.

| | measured | spec |
|---|---|---|
| `fdiv.s 1.0, 0.0` | **`0x7FC00000`** (a NaN) | `0x7F800000` (+inf), DZ |
| `fdiv.s -1.0, 0.0` | **`0x7FC00000`** | `0xFF800000` (-inf), DZ |
| `fdiv.s 0.0, 0.0` | `0x7FC00000` | NaN, but with NV, not DZ |

Canonicalization does not help and was not applied to hide it: after
`ad36904` the wrong answer is simply a *canonical* wrong answer. The
canonical-NaN tests deliberately leave `x/0` out, so that nothing pins
this behaviour.

Not fixed: it is a value defect in an op the canonical-NaN stage only
passed through, and it deserves its own red test and control.

---

## 76. FIXED: an FP instruction's integer source did not see the instruction before it

`fcvt.{s,d}.{w,wu,l,lu}`, `fmv.{w,d}.x` and the base address of
`flw`/`fld`/`fsw`/`fsd` read an **x** register. On the two forwarding
variants that read was taken straight from the architectural register
file in the execute stage, so a producer still in the pipeline was not
seen:

```
    li t0, 3
    fcvt.d.l f1, t0
    fmadd.d f4, f1, f1, f1
```

| model | f1 before | f4 before | after |
|---|---|---|---|
| rvss | `0x4008000000000000` | `0x4028000000000000` | unchanged ✓ |
| rv5-h-nf | `0x4008000000000000` | `0x4028000000000000` | unchanged ✓ |
| **rv5-h-f** | **`0x0`** | **`0x0`** | `0x4008…` / `0x4028…` |
| **rv5-nh-f** | **`0x0`** | **`0x0`** | `0x4008…` / `0x4028…` |
| rv5-nh-nf | `0x0` | `0x0` | still `0x0` — by design, see 77 |

Measured identically through a bare processor and through
`ProcessorManager::run()`, which is the path `MainWindow` drives. Padding
every dependent pair with four `nop`s made all five models correct, which
is what identified it as a hazard rather than an arithmetic fault.

Fixed in `6912b0c`, with the tests in `9ce7f61`
(`test/fp_int_source_hazard_test.cpp`, every case at producer distance 0,
1, 2 and 3 on all five models).

### Three defects, one root

Five bits of an instruction are read as both an x index and an f index,
and the datapath decided which one it meant by comparing `funct7` against
bare constants.

**1. The read bypassed forwarding, twice.** The FP execute path read
`registers_.ReadGpr(rs1)` before the forwarding mux and then **again
after it**, under a comment that called the second one a "re-apply
GPR-read override" — so a forwarded value would have been discarded even
if one had existed. And with forwarding enabled the hazard unit returns
`STALL_NONE` for everything but load-use (`rv5s_hdu.cpp`: *"All other
hazards … are solved by the forwarding unit"*), so the stall that makes
`h_nf` correct was deliberately suppressed. The override is gone; the
read now goes through `forwardedIntegerSource()`, which is the integer
path's own rule — EX/MEM, then MEM/WB, then the register file, with the
`rd != 0` guard — in one shared place so the two cannot drift.

**2. An R4-type is not a funct7, and this hit every model.** `fmadd`,
`fmsub`, `fnmadd` and `fnmsub` carry `rs3 << 2 | fmt` in bits 31:25. So
`rs3 = f26` reads `0b1101000`/`0b1101001` and `rs3 = f30` reads
`0b1111000`/`0b1111001` — exactly the four patterns that meant "this
instruction's rs1 is an integer register":

| | before | after | correct |
|---|---|---|---|
| `fmadd.d f4, f1, f2, f26` with 3, 5, 7 | **`0x401C000000000000`** (7.0, the value of f26) | `0x4036000000000000` | 22.0 |
| `fmadd.s f4, f1, f2, f30` with 3, 5, 7 | **`0x40E00000`** (7.0f) | `0x41B00000` | 22.0f |

It returned the addend because rs1 was replaced by `x[1]`, which was 0,
and `0*5+7 = 7`. **rvss, `h_nf` and `nh_nf` were wrong here too** — four
failing assertions each in the red run — so this half is not a pipeline
bug at all. The named predicate
`instruction_set::readsIntegerSource()` requires opcode `0b1010011`
before looking at funct7, and replaces all fourteen hand-written copies
(`rvss_processor.cpp`, `rv5s_processor_base.cpp`, and all four variants).
Same family as the writeback collision in 61, at the other end of the
datapath.

**3. Neither write-enable flag says which register file.** The control
unit sets `reg_write` for every OP-FP instruction and `freg_write` for
every F/D instruction, while `rd` and `frd` carry the same five bits. So:

| | before | after |
|---|---|---|
| `fcvt.d.l f5, t1` then a consumer reading **x5** | **`0x43D0070000000000`** — f5's bit pattern converted as an integer | `0x4008000000000000` |
| `fcvt.l.d t0, f6` (writes x5) then a consumer reading **f5** | **wrong** | correct |

`producerWritesGpr()` and `producerWritesFpr()` ask the instruction
instead, mirroring the writeback decision, and the FPR mux skips rs1
entirely when rs1 is an integer register.

`MEM_WB_Register` gained `prev_instruction` for this: at that stage
`prev_reg_write` and `prev_freg_write` are both set for an F/D
instruction, so the flags alone cannot tell the classes apart.

### Its mirror, and what was deliberately not changed

This entry fixed the FP side: an FP instruction reading an x register.
The integer side had the same hole in the other direction - an ordinary
integer instruction reading an x register whose index matched a pending
f destination - and it was not found until the follow-up stage widened
the test matrix. See **79**, fixed in `f8870d5`.

`h_nf` and `nh_nf` keep reading the architectural register file, which is
correct for their contract — `h_nf` stalls until the value is
architectural, `nh_nf` has no hazard handling by design — and took only
the predicate change. Five controls, each reverting one part of the fix,
fail a different named test; the full set is in the stage report.

---

## 77. `li` of a large constant is a dependent chain, and the no-hazard variants run it anyway

`li` is a pseudo-instruction. For anything outside a 12-bit signed
immediate the assembler expands it into a chain of `lui`, `slli` and
`addi` (`src/assembler/parse_formats/pseudo_formats.cpp`, the `li` arm),
each step depending on the previous, **with no `nop`s between them**.

`rv5-nh-nf` and `rv5-nh-f` have no hazard detection. Measured:

| | rvss | rv5-h-nf | rv5-h-f | **rv5-nh-nf** | **rv5-nh-f** |
|---|---|---|---|---|---|
| `li x5, 0x3FF0000000000000` then read x5 | `0x3FF0…` | `0x3FF0…` | `0x3FF0…` | **`0x0`** | **`0x0`** |
| `li t0, 3` (one `addi`) | 3 | 3 | 3 | 3 | 3 |

So a one-instruction `li` is fine and a multi-instruction one silently
produces garbage — on the two models a student is most likely to pick
when asked to see what a pipeline without interlocks does.

**This is the variants behaving as designed.** A processor with no hazard
detection does not interlock, and the contract says the programmer
inserts the `nop`s. The defect is that nothing says so: the assembler
expands `li` into a chain the chosen processor cannot execute, and
neither the editor nor the run reports anything. Every float test in this
repository builds its constants one byte at a time with four `nop`s
between dependent pairs for exactly this reason
(`test/float_test_support.h`).

**Correction: the rv5-nh-f cell was wrong.** Re-measured with the
out-of-tree `lirepro` harness, both stepped and through `DebugRun`, at
three commits:
- HEAD;
- `424b41d`, where this entry was written;
- `9ce7f61`, before the 76 fix.

At every one, `li x5, 0x3FF0000000000000` leaves x5 =
`0x3FF0000000000000` on nh-f, and so does a consumer back to back
(`addi x6, x5, 0`).

The expansion is `addi x5, x0, 1023; slli x5, x5, 52`
(`pseudo_formats.cpp:213-232`). Every `li` chain is lui/addi/slli: ALU
producers at distance 0, which nh-f's EX/MEM forwarding covers
(`rv5s_processor_nh_f.cpp:279`). So it is **not an nh-f forwarding
defect and not load-use**.

The one thing that did go wrong on nh-f was an *FP* consumer:
`fmv.d.x f1, x5` gave `f1 = 0x3FF` at `9ce7f61`. That was defect 76,
fixed in `6912b0c`. Only the nh-nf cell (x5 = 0) stands. It has no
forwarding, and `li` cannot be padded from the source.

**The pair that genuinely needs a nop on nh-f** is a load (`lb`, `lh`,
`lw`, `ld`, `lbu`, `lhu`, `lwu`, `flw`, `fld`) followed immediately by an
instruction reading its destination. nh-f never calls
`check_data_hazard`, and no forwarding path has the value before the end
of MEM. Measured with `ld x5; addi x6, x5, 0`: nh-f x6 = 0 at distance
0, right at distance 1.

The processor chooser's note for nh-f ("dependent instructions need nops
between them", `processor_choice.cpp:98-99`) therefore overstates it;
that note was deliberately not edited in this stage.

`test/li_forwarding_nh_f_test.cpp` pins both facts: five multi-instruction
constants with a distance-0 consumer, and load-use with one nop, on rvss,
nh-f, h-nf and h-f. It passes at base (there was nothing to fix). Its
control, disabling nh-f's EX/MEM forward to operand A
(`rv5s_processor_nh_f.cpp:279`), fails
`LiForwardingNhF.MultiInstructionLiAndItsConsumerBackToBack`.

Suggested, not implemented: a warning from the assembler when a
pseudo-instruction expands to more than one instruction and the selected
processor has no hazard detection — the expansion already knows how many
instructions it emitted, and `ProcessorManager` already knows the model.
Not done here: it crosses from the assembler into the UI's notion of the
current processor, and it needs its own design.

---

## 78. The hazard unit reads every instruction's rs1 as an f index too, and stalls on a bare index match

`check_data_hazard` (`src/processor/rv5s/rv5s_hdu.cpp:39-46`) pulls both
readings out of the same five bits, for every instruction, whatever its
class:

```cpp
uint8_t if_id_rs1  = (instruction >> 15) & 0x1F;   // GPR reading
uint8_t if_id_frs1 = (instruction >> 15) & 0x1F;   // FPR reading, same bits
```

The GPR half is right, and is why `h_nf` was correct throughout 76 — it
sees an FP instruction's integer source and stalls for it. The FPR half
has no such excuse: it compares an **integer** instruction's rs1/rs2
against a pending FP destination, so an index match with no dependency
raises `fpr_hazard` and stalls.

**Measured**, two programs identical but for one register number:

```
    fadd.d f5, f1, f2        # or f9
    addi   s0, t0, 1         # t0 is x5: index 5
```

| | rv5-h-nf | rv5-h-f |
|---|---|---|
| FP destination **f5**, consumer reads **x5** | **32 cycles** | 30 cycles |
| FP destination f9, consumer reads x5 | 30 cycles | 30 cycles |

So the bare index match costs **two cycles** on `h_nf`.

**`h_f` is affected too, and an earlier version of this entry said it was
not.** The claim was that forwarding mode returns `STALL_NONE` for
everything but load-use, so the false positive has no path to a stall.
It has one: a float **load** sets `id_ex_reg_.mem_read`, which is the
load-use branch, and `fpr_load_use` there compares `id_ex_reg_.frd`
against `if_id_frs1` — which for an integer consumer is simply its rs1
bits. Measured, `fld`/`flw` into fN followed by `addi s0, t0, 1` where
t0 is x5:

| | rv5-h-f | rv5-h-nf |
|---|---|---|
| `fld f5` — index 5 collides with rs1 | **21 cycles** | **22 cycles** |
| `fld f9` — no collision | 20 | 20 |
| `flw f5` | **21** | **22** |
| `flw f9` | 20 | 20 |

One cycle on `h_f`, two on `h_nf`, for a dependency that does not exist.
`s0` is `0x5B` (91) in all eight runs.

**It is a performance artefact, not a correctness one**: `s0` is
`0x5B` (91) in all four runs. A program's results do not change, only
its cycle count — which matters here, because cycle counts are the thing
this simulator exists to teach.

The fix is the same shape as 76 and 79: ask what the consumer reads
rather than matching an index. `instruction_set::readsIntegerSource()`
already exists for the rs1 half, and the FPR comparisons should apply
only to instructions that read F registers at those positions.

Not fixed: this stage was told to measure it and leave it. Note that
`stall_cycles_` reads 0 in all four runs, so the counter does not track
these stalls; the cycle count is the measurement.

---

## 79. FIXED: an FP result reached a plain integer consumer

The mirror of 76, in the **integer** forwarding block. 76 fixed the FP
side — an FP instruction reading an x register. This is an ordinary
integer instruction reading an x register whose index happens to match a
pending **f** destination.

`ex_mem_reg_.alu_result` is latched for every instruction
(`rv5s_processor_h_f.cpp`, the EX/MEM latch), the control unit sets
`reg_write` for every OP-FP instruction
(`rv5s_control_unit.cpp:91-93`), and `rd` carries the same five bits as
`frd`. The integer forwarding block asked only `reg_write && rd != 0`,
so an FP producer writing `f5` forwarded its floating-point result into
any consumer reading `x5`.

**Measured** before the fix, in a clean worktree at `424b41d` with only
the test added: **60 failing assertions on rv5-h-f and 60 on rv5-nh-f,
zero on rvss, rv5-h-nf and rv5-nh-nf.** Every producer reached every
consumer kind, at producer distance 0 and 1:

| consumer | role of the colliding register |
|---|---|
| `addi s0, t0, 1` | rs1 |
| `add s0, t5, t0` | rs2 |
| `sd t0, 0(t6)` | store data |
| `ld s0, 0(t0)` | load base |
| `beq t0, t5, skip` | branch operand |

with producers `fadd.d f5`, `fadd.d f10`, `fcvt.d.l f5`, `fmadd.d f10`,
`fld f10` and `flw f5`.

Fixed in `f8870d5`: the two integer forwarding guards in each forwarding
variant now ask `producerWritesGpr()` — the helper 76 already added —
instead of `reg_write` alone. Store-data forwarding reuses `forward_b`,
so the same two guards cover it.

**The guard is not too strict, and that is asserted rather than
assumed.** `FpProducersThatWriteAGprStillForward` covers the three FP
instructions that really do write an integer register — `feq.d` then
`addi`, `fcvt.l.d` then `add`, `fmv.x.d` then `sd` — all of which must
still forward, at distance 0 and 1, and do.

Five controls, one mutation each, each failing exactly one named test:
dropping the guard at the integer EX/MEM or MEM/WB site fails
`FpProducerDoesNotReachAPlainIntegerConsumer`; dropping it at either FP
site, or inside `forwardedIntegerSource`, fails
`AnIndexIsNotARegisterClass`.

Those same controls answer a question 76 left open — whether the
`fcvt.l.d` → `f5` alias passes at distance 2 and 3 because a guard works
or because the producer has gone. Dropping the FP EX/MEM guard fails
**distance 0 only**; dropping the FP MEM/WB guard fails **distance 1
only**. Neither control touches distances 2 and 3, so those pass
because the producer has left the pipeline and the consumer reads the
architectural register file, not because anything guards them.

**Not fixed, and not covered by any test:** `SetActiveWireNames()` in
both forwarding variants (`rv5s_processor_h_f.cpp:128,145` and
`rv5s_processor_nh_f.cpp:121,140`) tests `reg_write && rd != 0` the same
way, so the circuit diagram lights a forwarding wire that is not
forwarding. Cosmetic; no result depends on it.

---

## 80. `stall_cycles_` is a countdown, not a total, and the UI calls it "StallCycles"

`stall_cycles_` (`src/processor/processor_base.h:183`) is used by the
rv5s variants as the **number of stall cycles still owed**, not as a
running total. `RV5StageProcessorHF::Step` sets it to `stalls_needed - 1`
when a hazard starts (`rv5s_processor_h_f.cpp:231`) and decrements it
while the stall is being served (`:220`), so once the pipeline catches up
it is back to **0**.

Measured, the two programs from 78 and from the load-use measurement:
every run that provably stalled — `h_nf` taking 32 cycles where 30 would
do, `h_f` taking 21 where 20 would do — finished with
`stall_cycles_ == 0`. The cycle count is the only evidence a stall
happened.

Who reads it:

* `ProcessorManager::getStallCycles()` (`processor_manager.cpp:404`)
  returns it straight through;
* the processor tab shows it in the VM state table under the key
  **`StallCycles`** (`src/ui/processor_tab/vm_state_table_model.cpp:40`,
  `:92-93`);
* `ProcessorBase::DumpState` writes it to `vm_state.json` as
  `"stall_cycles"` (`processor_base.cpp:557`).

So a student watching that row sees 0 almost always, and a small number
only in the instant a stall is being served. The name promises a total.

Not fixed, and worth saying why it is not a one-liner: `Undo`/`Redo`
save and restore it (`rv5s_processor_base.cpp:897`, `:958`) as part of
the step delta, so changing its meaning changes what those replay. A
cumulative counter would want to be a second field.

---

## 81. FIXED: the circuit view lit a forwarding wire that was not forwarding

`SetActiveWireNames()` decided which forwarding wires to highlight with
the same bare test the datapath used before 79:

```cpp
if (ex_mem_reg_.reg_write && ex_mem_reg_.rd != 0)            // h_f.cpp:128
if (mem_wb_reg_.prev_reg_write && mem_wb_reg_.prev_rd != 0)  // h_f.cpp:145
```

and the same two in `rv5s_processor_nh_f.cpp:121,140`. `reg_write` is
set for every OP-FP instruction and `rd` carries the same five bits as
`frd`, so `fadd.d f5, …` followed by any instruction reading x5 lit the
integer forwarding path although after 79 nothing travels along it.

**Fixed**, with the same substitution 79 made, at all four sites:
`producerWritesGpr(…instruction, …reg_write)` from
`rv5s_forwarding.h:55`, which both files already included.

**There was no FPR site to change.** All four guards cover GPR-indexed
wires, and neither `SetActiveWireNames` lights an FPR forwarding wire at
all — worth recording, because the entry as filed implied there might be.

Measured on h-f and nh-f, union of `active_wires_` over every cycle,
before → after. The three wires are the only CONDITIONAL ones:
`P2_to_FDU_rs1`, `P2_to_FDU_RS2` and `P4EB_to_FWD_unit` are in the
constructor's backbone (`h_f.cpp:68-70`) and are on regardless, which is
why the original entry's list of four was half backbone.

| producer, then a consumer reading t0 (x5) | `FDU_to_FMUX1_cntrl` | `FDU_to_fMUX2` | `FDU_to_FMUX3` |
|---|---|---|---|
| `fadd.d f5`, adjacent | 1 → **0** | 1 → **0** | 1 → **0** |
| `fadd.d f5`, two nops apart | 1 → **0** | 0 → 0 | 1 → **0** |
| `addi t0`, adjacent | 1 → 1 | 1 → 1 | 1 → 1 |
| `addi t0`, two nops apart | 1 → 1 | 0 → 0 | 1 → 1 |
| `fcvt.l.d t0`, adjacent | 1 → 1 | 1 → 1 | 1 → 1 |
| `fcvt.l.d t0`, two nops apart | 1 → 1 | 0 → 0 | 1 → 1 |

`fcvt.l.d` is the case that stops the fix over-reaching: it is an F
instruction that really does write the integer register t0 (funct7
`1100001`), and `producerWritesGpr` recognises it, so its wires stay
lit.

Covered by `test/forwarding_wire_test.cpp`. No Spy was needed:
`active_wires_` (`processor_base.h:193`) and `SetActiveWireNames()`
(`:288`) are both public, and `StepAndNotify` calls the latter every
cycle (`rv5s_processor_base.cpp:69`).

Still **not run in the GUI** — the assertion is about the contents of
`active_wires_`, which is what `CircuitScene::updateCircuitState` reads
(`circuit_scene.cpp:156-184`); nobody opened a window and watched a wire
go dark.

Two things found beside it, both open: the distances at which these
wires light are not the distances at which the machine forwards (95),
and nothing checked that a lit name is a wire that exists (93).

---

## 82. FIXED: Undo into the middle of a program, then continue, lost every register write

Found while testing that `MEM_WB_Register::prev_instruction` survives an
Undo. It does. This was a different defect, and an older one: measured
identical at `47ff007`, before any commit on the branch that added that
field.

```
    li t0, 3
    fcvt.d.l f1, t0
    fmadd.d f4, f1, f1, f1
```

| run, `rv5-h-f` | pc | cycles | retired | x5 | f1 | f4 |
|---|---|---|---|---|---|---|
| straight, 10 steps | 40 | 10 | 3 | **3** | `0x4008…` | `0x4028…` |
| step 3, **undo 1**, step 8 — before | 40 | 10 | 3 | **0** | **0** | **0** |
| step 3, **undo 1**, step 8 — after | 40 | 10 | 3 | 3 | `0x4008…` | `0x4028…` |

Undoing **to the start** replayed correctly and undoing the **last** few
cycles replayed correctly, which is why the two existing undo tests
missed it; an Undo that landed in the middle and was then continued
finished with the right PC, the right cycle count and the right retired
count and the wrong architectural state.

### What it really was

`RV5SControlUnit::GetAluSignal` (`src/processor/rv5s/rv5s_control_unit.cpp:105`)
discarded its `ALUOp` argument — it was `(void)ALUOp;`, with a comment
saying the boolean "is ignored in this design" — and switched on the
**member** `alu_op_`, declared in
`ControlUnit` (`src/processor/control_unit_base.h:53`) and written only
by `SetControlSignals()`, which only `pipeline_decode()` calls
(`rv5s_processor_base.cpp:199`).

`Step()` runs the stages backwards — WB, MEM, EX, ID, IF
(`rv5s_processor_h_f.cpp:203-256`) — so the Execute stage read the row
the **previous** cycle's decode left behind. Correct while the machine
only ever moves forwards, and invisible for exactly that reason. It is
the same "the cycle runs backwards" arrangement that
`MEM_WB_Register::prev_*` exists for (`rv5s_processor_base.cpp:440-455`),
except that `prev_*` lives in a pipeline register and `alu_op_` does not.

`RV5StageStepDelta` and `PipelineRegisterChange`
(`rv5s_processor_base.h:9-27`) carry the four pipeline registers and the
counters, **not `control_unit_`**. So `Undo()` (`:853-916`) restored
every pipeline register byte for byte and left `alu_op_` holding the
undone cycle's row. Traced on `rv5-h-f`, the straight run and the replay
at the same cycle:

```
straight  cyc=2 CU.alu_op=1 | ID/EX 00300293 rd=5 rw=1 imm=3 asrc=1 aop=1
UNDO      cyc=2 CU.alu_op=3 | ID/EX 00300293 rd=5 rw=1 imm=3 asrc=1 aop=1
```

Every pipeline register matches; `alu_op_` is 3 where it should be 1.
The replayed `addi t0, x0, 3` then took ALUOp row 3 instead of row 1 and
latched `ex_mem_reg_.alu_result = 0`. x5 = 0 follows, then f1 = 0.0 by
forwarding, then f4 = 0.

**It was not only registers.** On a program that stores, the same wrong
row produced a wrong address:

```
straight  cyc=8 CU.alu_op=0 | EX/MEM 10703023 alu=0x0000000000000100   # sd t2, 256(x0)
replay    cyc=8 CU.alu_op=1 | EX/MEM 10703023 alu=0x0000000000000001
```

Row 1 with `sd`'s funct3 (`0b011`) is `SLTU`, and `SLTU(x0, 256)` is 1,
so the store went to **address 1** and overwrote the program's own
instruction bytes; the later `ld` read them back as data
(`x28 = 0x130000001305A003`).

Three things that were suspected and are **not** the cause, each checked
in the code: the redo stack **is** cleared on a new step
(`finalize_step_delta`, `rv5s_processor_base.cpp:150-154`); register
changes are recorded as (old, new) pairs (`:617`, `:701`, `:750`) and
re-applied correctly in both directions; and no write-enable or
write-back latch is mis-restored.

### Measured, before and after

Every `(N, k)` with `1 ≤ k ≤ N ≤ T`, in two sequences — `step N, undo k,
step (T−N+k)` and `step N, undo k, redo k, step (T−N)` — compared
against a straight run of `T` cycles on all 32 GPRs, all 32 FPRs, 512
bytes of memory, the PC, the cycle count, the retired count,
`stall_cycles_` and `branch_mispredictions_`. Cells that ended somewhere
other than the straight run:

| program (T) | cells | rvss | nh-nf | nh-f | h-nf | h-f |
|---|---|---|---|---|---|---|
| the reported program, padded (16) | 272 | 0 | **15** | **15** | **15** | **15** |
| the reported program, unpadded (12) | 156 | 0 | **10** | **19** | **13** | **19** |
| a store and a load back (21) | 462 | 0 | **10** | **10** | **10** | **10** |
| a loop with a taken branch (36) | 1332 | 0 | 0 | 0 | 0 | 0 |

After the fix every one of those cells is 0, on all five models and all
four programs. **RVSS never had it**: it has no pipeline, so decode and
execute run in the same `Step()` and the member is always the row the
instruction in EX wants.

The failures land on particular cycles, the ones where the undone
decode's row differs from the row the replayed EX needs: cycles 2 and 7
for the padded program, 7 and 12 for the store program, and 2, 3 or 5
for the unpadded one depending on the model. Five of the ten store-
program cells on each variant also lost the stored byte and corrupted
the text segment.

**Every failing cell was `step → undo → step`.** `step → undo → redo →
step` never failed, because `Redo` leaves the machine at cycle N, which
is exactly where the last forward decode left `alu_op_` — the lost state
happened to already hold the right value. That is why
`test/rv5s_undo_redo_test.cpp`, which only ever does Step → Undo →
**Redo**, never caught it, and the UI offers precisely the sequence that
breaks: undo a cycle, then press Step.

The same matrix driven through `ProcessorManager` — `main_window.cpp:379-381`
→ `processor_manager.cpp:278, 313, 317` → `StepAndNotify()` / `Undo()` /
`Redo()`, i.e. what the toolbar buttons call — reproduces it with the
same landing cycles, and is clean after the fix. That matrix has to stop
at the drain point: `ProcessorManager::step()` calls `RequestStop()` once
`StepAndNotify()` reports the program exhausted
(`processor_manager.cpp:282-286`), and `RequestStop()` is a full
`StopAndReset()` (`processor_base.cpp:190-212`), so stepping past the end
through the UI path wipes the processor by design.

### The fix

`ID_EX_Register::alu_op` (`pipeline_registers.h:71`) already holds the
same row, latched at decode (`rv5s_processor_base.cpp:243`) and restored
by `Undo` with the rest of the register. The new
`RV5SControlUnit::GetAluSignalFor(uint32_t instruction, uint8_t alu_op)`
takes the row as an argument and holds the table;
`GetAluSignal(uint32_t, bool)` keeps its virtual signature and delegates
with `alu_op_`, so `ControlUnit` and RVSS are untouched. All fourteen
rv5s call sites pass `id_ex_reg_.alu_op`: twelve live (three in each of
`rv5s_processor_h_f.cpp`, `h_nf.cpp`, `nh_f.cpp`, `nh_nf.cpp`) and two in
`rv5s_processor_base.cpp:782, :818`, inside `execute_float` and
`execute_double`, which have no callers.

Nothing about the ALU row now survives a cycle boundary, so there is
nothing for a snapshot to miss. One deliberate behaviour change: on a
stall ID/EX is a bubble with `alu_op = 0`, where `alu_op_` kept the
stalled instruction's row, so a bubble's `alu_result` can differ. A
bubble carries `rd = 0` and `reg_write = false`, so nothing forwards or
writes back from it; the suite and the live sweep are the check.

`test/rv5s_undo_continue_test.cpp` sweeps the whole matrix. Controls, in
a throwaway worktree at the fix commit: reverting **one** call site
(`rv5s_processor_h_f.cpp:363`) fails `rv5-h-f` alone, 10 cells per
program; reverting all twelve reproduces the table above exactly;
bending one bit of the oracle fails all 272 of 272 cells on all five
models, so the comparison is live.

---

## 83. The cache's own undo history is never called, so Undo doubles the counters

`Cache` has a per-step undo history — `beginStep`, `commitStep`,
`undoStep`, `redoStep` (`src/processor/cache/cache.cpp:329, 339, 358,
368`, declared `cache.h:118-121`, with a comment saying one entry is
pushed per processor step so "the history stays in step with the
processor's"). **Nothing calls any of the four**, in `src/` or in
`test/`. The processors' `Undo` rewinds memory byte by byte through
`memory_controller_.writeByte` (`rv5s_processor_base.cpp:882-890`) —
deliberately through the cache, so the undone value does not linger in a
dirty line — and never touches the cache's line state, its replacement
order or its statistics.

Measured on the store-and-load program, 21 cycles, step 17 / undo 10 /
continue, after the fix for 82, so the architectural state matches
exactly:

| | L1 hit | L1 miss | L2 hit | L2 miss | I$ hit | I$ miss |
|---|---|---|---|---|---|---|
| straight, `rv5-h-f` | 0 | 2 | 0 | 13 | 12 | 4 |
| replay, `rv5-h-f` | **10** | 2 | **9** | **16** | **18** | **7** |
| straight, rvss | 0 | 2 | 0 | 13 | 12 | 4 |
| replay, rvss | **1** | 2 | 0 | **16** | **19** | **7** |

Two things inflate them: the replayed cycles access the cache a second
time, and `Undo`'s own `writeByte` calls count as accesses and allocate
lines. So the cache statistics the UI shows, and anything derived from
them, are not a function of the architectural state once Undo has been
used — they are a function of how the user got there. The architectural
state is correct; only the bookkeeping is not. RVSS has this too.

Not fixed here. The machinery to fix it already exists and is unwired:
the processors would have to call `beginStep()`/`commitStep()` around
each step and `undoStep()`/`redoStep()` from `Undo`/`Redo`, and `Undo`
would then rewind the bytes without going through the cache at all.

---

## 84. FIXED: the processor chooser matched its own rows by text, and preselected by enum order

The chooser was a `QTreeWidget` with five rows of literal text declared
in `src/ui/dialogs/processor_dialog.ui`, and the selection was turned
back into a `ProcessorType` by comparing the **selected row's text**
against five string literals in the `.cpp`:

```cpp
QString processorType = selectedItem->text(0);
if (processorType == "Single cycle processor")                       // :27
    emit vmSelected(ProcessorType::RVSS);
else if (processorType == "5 statge Processor w/o hazard detection w/o forwarding")
    emit vmSelected(ProcessorType::RV5Stage_NH_NF);                  // :33
...
```

Two faults, neither of which could bite a test, because nothing tested
the dialog:

1. **The strings are the API.** Rewording a row, fixing its spelling, or
   translating it makes the chain fall through every arm and emit
   nothing at all - the dialog closes, `vmSelected` never fires, and the
   processor silently does not change. Three of the five rows read
   "5 **statge**", so the typo could not be corrected without breaking
   the dialog.
2. **The preselect assumed the .ui's row order was the enum's.**

```cpp
ui->treeWidget->setCurrentItem(ui->treeWidget->topLevelItem(static_cast<int>(currentVMType)));
```

   Reordering the rows in Designer, or inserting a model into
   `ProcessorType` anywhere but the end, would open the dialog showing a
   different processor from the one that is loaded. Nothing asserted the
   two orders agreed.

**Fixed.** The tree is gone. Two radio buttons and two checkboxes
describe the processor by what it HAS, and the relation between three
check marks and five models is four free functions with no Qt in them,
`src/processor/processor_choice.{h,cpp}`:

```cpp
ProcessorType   processorTypeFor(bool pipeline, bool hazard, bool forwarding);
ProcessorChoice processorChoiceFor(ProcessorType type);
const char     *processorDescription(ProcessorType type);
const char     *processorNote(bool pipeline, bool hazard, bool forwarding);
```

`test/processor_choice_test.cpp` pins all eight combinations and the
round trip for all five types without constructing a widget;
`test/processor_dialog_test.cpp` drives the dialog offscreen. There is no
string comparison and no `static_cast<int>` preselect left anywhere.

Controls, on patched copies out of tree at the fix commit:

| control | red tests, of 25 |
|---|---|
| (a) `(pipeline, hazard, no forwarding)` returns `NH_NF` instead of `H_NF` | **4** - `ProcessorChoice.EveryCombinationOfTheThreeControlsMapsToOneModel`, `.TheInverseRoundTripsForEveryProcessorType`, `ProcessorChooserDialog.TheRoundTripThroughSingleCycleKeepsTheModel`, `.OkEmitsTheModelTheControlsSpellOut` |
| (b) the check boxes ignore the radio state and stay live | **2** - `ProcessorChooserDialog.ItOpensShowingTheProcessorThatIsLoaded`, `.SingleCycleGreysTheBoxesWithoutClearingThem` |
| (c) the status label is not refreshed on a change | **2** - `ProcessorStatusLabel.ItFollowsEveryProcessorChange`, `.TheFiveLabelsAreAllDifferent` |

The two checkboxes are greyed, not cleared, when Single-cycle is
selected, so Pipeline -> Single-cycle -> Pipeline returns the same model.
Opened on Single-cycle they are greyed but **ticked**: ticking Pipeline
then lands on hazard detection + forwarding rather than on
`RV5Stage_NH_NF`, the variant that silently evaluates a dependent chain
to zero (entry 77).

The note under the controls is entry 77's "suggested, not implemented"
warning in its cheapest form - not an assembler diagnostic, just a line
in the chooser saying what the chosen combination costs. Each clause is
this code's behaviour: with forwarding the hazard unit stalls only on a
load-use (`src/processor/rv5s/rv5s_hdu.cpp:82-104`); without it, until
the producer has written back (`:106-117`); and
`rv5s_processor_nh_nf.cpp` and `rv5s_processor_nh_f.cpp` never call
`check_data_hazard` at all.

---

## 85. The Profiler's own counters are dead: one is never written, one only grows, and nothing resets either

`Profiler` (`src/profiler/profiler.h`) keeps three maps and a count
array. Two of the four are not maintained.

**`getLineExecutionCount()` always returns 0.** It reads
`m_lineNumberToExecutionCounts` (`src/profiler/profiler.h:52`), and that
member is only ever cleared (`src/profiler/profiler.cpp:15`) and read
(`:47-48`). Nothing in `src/` writes it. The counting happens on the far
side of the signal, in a member of the same name belonging to a
different class:

```cpp
void ProfilerEditor::incrementLineExecutionCountSlot(int lineNumber)
{
    m_lineNumberToExecutionCounts[lineNumber]++;   // profiler_editor.cpp:113
}
```

read back by the gutter at
`src/ui/profiler_tab/execution_count_gutter_column.h:39-40`. So the
numbers exist, but only inside the widget that draws them; the Profiler's
public accessor for them is a constant zero. Measured during this stage:
`test/processor_switch_profiler_test.cpp` had to count signal emissions
instead, because the accessor could not distinguish "profiling works"
from "profiling is unplugged".

**`m_instructionTypeCounts` accumulates across loads.**
`setLineNumberToInstructionTypeMapping` clears
`m_lineNumberToinstructionType` but not the count array, then increments
into it (`profiler.cpp:57-69`). `ProcessorManager::loadProgram` calls it
on every load (`processor_manager.cpp:86`), so loading the same program
twice reports every instruction type twice. The commented-out
`setInstructionTypeCounts` just above it (`profiler.cpp:25-38`) did start
with `m_instructionTypeCounts.fill(0)`.

**Nothing clears either.** `Profiler::Reset()` (`profiler.cpp:13-18`) is
the function that would, and it has **no callers** anywhere in `src/` or
`test/`. `ProfilerTab` connects `profilerReset` to
`ProfilerTab::resetProfilerView`, whose body is empty
(`profiler_tab.cpp:68-71`).

**`updateInstructionTypeCountsSignal` has a connection and no emitter.**
`ProfilerTab` connects it to the table model
(`profiler_tab.cpp:37-38`); the only `emit` of it in the tree is inside
the commented-out block at `profiler.cpp:37`. The instruction-type table
is therefore filled once at construction
(`profiler_tab.cpp:26-28`) and never updated.

**Read, not run**, except for the first point, which the stage's test
exercised directly. Not fixed here: this is a UI-stage branch and the
profiler is its own piece of work. The shape of the fix is to make
`Profiler` the one that counts - have `processorClockedSlot` increment
its own map before emitting - and to call `Reset()` from `loadProgram`.

Separately, `processorClockedSlot` subscripts with `operator[]`:

```cpp
int executedLine = m_instructionNumberToLineNumber
[static_cast<unsigned int>(processorState.lastExecutedPC / 4)];   // profiler.cpp:75-76
```

so a PC with no mapping - `INVALID_PC / 4` on any pipeline bubble -
inserts a new entry and reports line 0 rather than being skipped.

---

## 86. A pipelined `Run()` stops dead on a breakpoint `LoadProgram` planted, and the GUI only escapes it by accident

`ProcessorBase::LoadProgram` ends every load by planting a breakpoint at
the address one past the last instruction:

```cpp
program_size_ = counter;
AddBreakpoint(program_size_, false); // address      // processor_base.cpp:256-257
```

`RVSSProcessor::Run()` never reaches it: its loop is
`while (!stop_requested_ && program_counter_ < program_size_)`
(`rvss_processor.cpp:117`), so it exits the moment the PC gets there.

`RV5StageVM_Base::Run()` keeps going while the pipeline drains:

```cpp
while (!stop_requested_ && (program_counter_ < program_size_ || !is_pipeline_drained()))
```
(`rv5s_processor_base.cpp:11`). Those drain cycles run with
`program_counter_ == program_size_`, which **is** the planted
breakpoint, so the loop sets `pause_requested_`, emits
`processorPausedAtBreakpointSignal` and waits on
`pause_wait_condition_` (`:20-33`) with nobody to resume it.

**Measured.** A test that called `ProcessorManager::run()` after
`loadProgram` hung on every rv5s model; four `gdb` backtraces taken five
seconds apart were identical, all parked in
`QWaitCondition::wait` inside `Kites::RV5StageVM_Base::Run()`. RVSS
completed the same program in 0.04 s.

**The GUI does not hit this, for a reason that is not the breakpoint's.**
`MainWindow::tryParseAndLoadProgram` calls
`setBreakpoints(editor->getBreakpoints())` immediately after
`loadProgram` (`main_window.cpp:896-897`), and
`ProcessorBase::SetBreakpoints` **clears the list first**
(`processor_base.cpp:428-435`). So the planted breakpoint is wiped on
every load, by a call made for an unrelated purpose. Remove or reorder
that one line and every pipelined Run in the window stops at the end of
the program instead of finishing.

**`kites-cli` would hit it, and cannot be used to show it.** `ExecuteLoad`
calls `manager.loadProgram` and then clears only its own
`state.breakpoints` (`command_handler.cpp:188-191`); nothing pushes an
empty list down to the processor, and `CommandType::RUN` calls
`manager.run()` directly (`:277`). So `load <file> --vm rv5-h-f` then
`run` has nothing to wipe the planted breakpoint with. That is **read,
not run**: the REPL never gets that far, because it segmentation-faults
during startup - see entry 87.

Not fixed here. Two candidate fixes, neither obviously right without
knowing what the planted breakpoint is for: stop planting it and let the
loop condition end the run, or have the rv5s loop ignore a breakpoint at
`program_size_` while draining.

---

## 87. `kites-cli` segmentation-faults on startup: a QCoreApplication cannot hold a QGraphicsScene

`cli/main.cpp` constructs a **`QCoreApplication`**
(`cli/main.cpp:51`, included at `:14`), not a `QGuiApplication`. `ProcessorManager`'s
constructor builds a processor (`processor_manager.cpp:40`), every
processor's constructor builds its circuit scene
(`rvss_processor.cpp:34` and the four rv5s equivalents), and
`CircuitScene` derives from `QGraphicsScene`
(`src/ui/processor_tab/circuit_scene.h:15`), which requires a
`QGuiApplication`.

Measured, under `run_hermetic.sh`, piping `load examples/ex_mem_fwd.s
--vm rvss` / `run` / `exit` into it: exit status **139** (SIGSEGV) with
no output at all, i.e. before a single command is read. The backtrace:

```
#0  QtPrivate::QPodArrayOps<QGraphicsScene*>::emplace<...>   libQt6Widgets
#1  QGraphicsScenePrivate::init()                            libQt6Widgets
#2  Kites::CircuitScene::CircuitScene(QObject*)
#3  Kites::RVSSCircuitScene::RVSSCircuitScene(QObject*)
#5  Kites::RVSSProcessor::RVSSProcessor()
#7  Kites::ProcessorFactory::RegisterVM<Kites::RVSSProcessor>(...)::{lambda()#1}
```

This is the same root cause that `AGENTS.md` records for the test
harness - "constructing any RVSS/RV5S processor builds a QGraphicsScene,
which crashed with SIGSEGV when uninitialized in headless environments" -
fixed there by giving `test/test_main.cpp` an offscreen `QApplication`
(`test_main.cpp:75-76, 104`), and never fixed in `cli/main.cpp`.

`kites_cli` is built with `-DDISABLE_GUI` (`CMakeLists.txt:140`), but
`DISABLE_GUI` does not guard the circuit scene: `processor_base.h:199`
holds a `std::unique_ptr<CircuitScene>` unconditionally.

**Pre-existing**, not introduced by any recent branch: the same input
gives exit 139 on `experiments/post-ribbon` as well. Not fixed here -
the cheap fix is an offscreen `QApplication` in `cli/main.cpp`, the
honest fix is to stop making the simulator own a widget.

---

## 88. Every theme fills a Disabled palette group that the stylesheet makes unreachable

`ThemeManager` builds a `QPalette` for each theme and fills the
`QPalette::Disabled` group deliberately:

```cpp
p.setColor(QPalette::Disabled, QPalette::WindowText, mutedForeground);  // theme_manager.cpp:338
p.setColor(QPalette::Disabled, QPalette::Text,       mutedForeground);  // :340
p.setColor(QPalette::Disabled, QPalette::ButtonText, mutedForeground);  // :342
```

and then the theme stylesheet opens with

```qss
QWidget {
    background-color: {{background}};
    color: {{foreground}};        /* template.qss:11-17 */
    ...
}
```

A Qt stylesheet `color` applies to the widget in **every** state. Once
`QWidget { color: … }` is set, the Disabled palette entry above is never
consulted for any widget that does not have an explicit `:disabled` rule
of its own. The buttons do have one (`template.qss:112`, `:143`, `:362`,
`:454`, `:525`, numbered after the rule added below), which is why a
disabled ribbon command visibly recedes.
Nothing else does.

**Measured.** Offscreen grabs of the processor chooser with the two
option check boxes disabled and then enabled, cropped to the same
rectangle around the two rows, mean luminance:

| theme | before: disabled / enabled | after: disabled / enabled |
|---|---|---|
| kites-dark | 45.2 / 45.2 (delta **0.0**) | 38.9 / 44.1 (delta -5.1) |
| kites-light | 231.1 / 225.5 (delta +5.6) | 235.4 / 226.9 (delta +8.5) |

A dimmer disabled row means a lower number on the dark background and a
higher one on the light. Before the fix the dark theme rendered the two
states **identically**; the light theme's small residual difference is
the indicator alone, which Fusion draws from the palette rather than
from the stylesheet. The label - the part a reader actually sees - did
not change in either.

Partly addressed: `QCheckBox:disabled { color: {{mutedForeground}}; }`
was added at `template.qss:31` for this stage, because the processor
chooser greys two check boxes and that has to be visible. The
**indicator** is still drawn at full contrast - adding a
`QCheckBox::indicator` rule would replace Fusion's drawing of the box
and the tick entirely, which is a theme job and not this one's.

Still unreachable for `QRadioButton`, `QLabel`, `QLineEdit`,
`QComboBox`, `QTableView` and every other class without a `:disabled`
rule. The general fix is one block listing the classes, or dropping
`color` from the blanket `QWidget` rule and letting the palette do its
job - the second is correct and the larger change.

---

## 89. FIXED: both data panels put their controls below the data they described

The Memory tab's `QGridLayout` had `memoryTableView` at `row 0, colspan 5`
and every control on `row 2` - the `Display Type:` label and its combo,
the `Search Memory` label, the search box and the Search button
(`memory_tab.ui:22-69` before this stage). The register panel had
`sectionsContainer` at `row 0` and `Display Type` at `row 1`
(`register_container.ui:16-46`). Both therefore read bottom-up: the
control that says how a column is formatted sat as far from that
column's header as the panel allowed.

Measured offscreen at 1200x800, in panel coordinates, before and after:

| | before | after |
|---|---|---|
| memory table | y=9..577 | y=49..617 |
| Display Type combo | y=585..615 | y=11..41 |
| Byte order combo | (did not exist) | y=11..41 |
| search box | y=586..613 | y=12..39 |
| Search button | y=583..617 | y=9..43 |
| register sections | y=9..581 | y=45..617 |
| register Display Type | y=587..617 | y=9..39 |

**Fixed**, in the `.ui` files only. The memory controls moved into a
nested `QHBoxLayout` on `row 0` with the table beneath, a nested box
rather than renumbered grid columns because the row gained two widgets
and the search box should take the slack; the register combo moved to
`row 0` with the sections beneath. Neither `.cpp` needed a line changed:
both address their widgets through `ui->`, never by position.

Object names are unchanged, which matters more than it looks. Each of
these controls has a ribbon twin, and the five tests that pin that sync
in both directions - `RibbonStage3.MemoryDisplayBaseReachesTheComboAndTheModel`
(`ribbon_navigation_test.cpp:951`), `.MemoryDisplayBaseMirrorsBackFromTheCombo`
(`:1001`), `.MemorySearchPressesTheTabsOwnButton` (`:1025`),
`.RegisterDisplayBaseReachesTheComboAndTheModel` (`:1126`) and
`.RegisterDisplayBaseMirrorsBackFromTheCombo` (`:1165`) - all reach their
widgets by objectName, so they went on passing unchanged and were the
regression net for the move.

`test/memory_layout_test.cpp` asserts the geometry rather than `.ui` row
numbers, because a row index is not what a reader sees: a widget can be
on row 0 and still be drawn last inside a nested layout. It also ties
the sync to the geometry - the widget the ribbon drives is asserted to be
the one now at the top - so the two cannot drift apart.

---

## 90. FIXED: the Double Word column had one byte order, no way to change it, and no label saying which

`MemoryModel::data` read the Double Word cell with
`MemoryController::readDoubleWord_d`, which reaches
`MainMemory::readGeneric` and assembles the eight bytes
little-endian (`main_memory.cpp:79`), and showed the result unchanged.
The header said `"Double Word"` and nothing else, so the eight bytes
`01 02 .. 08` read back as `0x807060504030201` with no indication that
that was a choice at all.

**Fixed as a display setting and nothing more.** `ByteOrder` lives in
`src/ui/common/display_base_types.h` beside `Base`, because it is the
same kind of thing - how a number is written, not what the number is -
and it reaches exactly one place, the Double Word arm of
`MemoryModel::data`. When Big is selected the already-read value is
passed through `qbswap` **before** formatting, so the reversal holds in
hex, decimal and binary alike. The header names the order in both states,
`"Double Word (LE)"` / `"Double Word (BE)"`, rather than marking only the
unusual one.

What deliberately did **not** change, and is asserted in
`test/memory_endian_test.cpp`: the bytes in memory, the Byte 0-7 columns
(still in address order), the high-address-at-top row order
(`memory_model.cpp:189`), and `MainMemory`/`Cache` byte assembly, which
stay little-endian and must not become parameterised by this. Nothing
under `src/processor` knows the enum exists, and the A/B engine
transcript was byte-identical across the change.

The cache counters cannot move either, and for a reason worth recording:
the model reads through the `_d` family (`memory_model.cpp:237, 254`),
which bypasses L1 and L2 entirely (`memory_controller.cpp:69-87`), so
**displaying memory has never touched a cache statistic**. The test pins
that rather than assuming it.

One thing this stage broke and fixed inside itself: with ten columns
under `QHeaderView::Stretch`, `"Double Word (LE)"` did not fit and
rendered as `"...Word (LE)"` - the suffix added to remove an ambiguity
produced one. Caught by the after-screenshot, not by a test. Column 1 is
now sized to its contents, the same per-section override the register
tables already use (`register_container.cpp:114-116`), and
`MemoryPanelLayout.TheDoubleWordHeaderIsWideEnoughToReadInBothOrders`
asserts the section is at least as wide as the text it has to draw.

Cells are not editable, and nothing in the application is: no model in
`src/ui` implements `flags()` or `setData()`. If editing is ever added,
the rule the big-endian view needs is that an edit must be un-swapped
before it reaches `writeDoubleWord`, or that the Double Word cell is
read-only while the view is big-endian and edits go through the
unambiguous Byte 0-7 columns.

---

## 91. A `MemoryTab` that is never shown has exactly one row, and a test can mistake that for centring

`MemoryTab`'s constructor calls `m_memoryModel->setRowsVisible(1)`
(`memory_tab.cpp:72`) and the real count only ever arrives from
`MemoryTableView::resizeEvent` → `MemoryTab`'s `resized` handler
(`memory_table_view.cpp:21-25`, `memory_tab.cpp:85-94`), which needs the
widget to be shown and laid out.

So a `MainWindow` constructed in a test but never `show()`n leaves the
memory model one row tall. `MemoryModel::data` computes its row offset as
`((((m_rowsVisible * 8) / 2) / 8) * 8) - row * 8` (`memory_model.cpp:189`),
which is 0 for a single row - so row 0 *is* the central address, and
`setCentralAddress(0x100)` appears to put `0x100` on row 0.

It does not. `setCentralAddress` centres (`memory_model.cpp:118-124`), and
in a shown window the same search puts `0x100` in the middle with row 0
showing **`0x160`** - measured while writing
`MemoryPanelLayout.TheRibbonStillDrivesTheWidgetsThatMovedToTheTop`,
which first carried the row-0 assertion copied from
`ribbon_navigation_test.cpp:1057` and failed on it.

That existing assertion is not wrong today, because that test never shows
its window. It is **fragile**: adding a `show()` to
`RibbonStage3.MemorySearchPressesTheTabsOwnButton` would break an
assertion about search, for a reason that has nothing to do with search.

Not changed here - it is someone else's test and it passes. The fix, when
that file is next touched, is to assert that the searched address is
somewhere in view and that the view moved, which is what the new test
does.

---

## 92. A rewritten `.ui` can be skipped by the next incremental build, and the binary keeps the old layout

Measured, while running this stage's negative control (c), which moves
the Memory tab's controls row back below the table:

1. the `.ui` was rewritten and `ninja -C build-tests -j4` run once;
2. ninja reported work and linked `kites_test`;
3. `ctest` passed **15 of 15** - including the three geometry tests that
   exist precisely to catch that move;
4. running ninja a second time, by hand, rebuilt the autogen step, and
   the same `ctest` invocation then failed the expected three:
   `MemoryPanelLayout.EveryControlIsAboveTheTable`,
   `.TheByteOrderControlIsInTheTopRowBesideDisplayType` and
   `.TheRibbonStillDrivesTheWidgetsThatMovedToTheTop`.

So the first build linked a binary built from the **old**
`ui_memory_tab.h`. Pushing the `.ui`'s mtime forward before building
(`touch -d '+1 minute'`) makes one pass enough, every time, which points
at CMake's AUTOUIC deciding by timestamp whether to re-run `uic`: a
`.ui` rewritten within the same second as the previous autogen run can
compare as not-newer. The exact rule inside AUTOUIC was **not** read -
what is measured is the behaviour above and that a timestamp bump fixes
it.

Why it matters beyond the control: edit a `.ui`, rebuild, run the app,
and the window can come up with the previous layout. The build reports
success, nothing warns, and the natural conclusion is that the edit was
wrong. It also means a `.ui`-only change can pass CI on a warm build
directory and behave differently on a cold one.

Not fixed here. The reliable workaround is a second `ninja` pass, or
removing `<build>/kites_core_autogen` before building, after any `.ui`
edit. A real fix belongs in the build files and wants someone to read
what AUTOUIC actually compares.

---

## 93. FIXED: nothing checked that a wire name a processor lights is a wire that exists

The circuit view highlights by **name**. Each processor appends strings
to `active_wires_` and `CircuitScene::updateCircuitState`
(`circuit_scene.cpp:156-184`) turns on every `WireItem` whose
`getName()` is in that list:

```cpp
if (wireList.contains(wire->getName()))
    wire->setActive(true);
```

A name no wire carries lights nothing and says nothing - no warning, no
log line, no crash. It is indistinguishable from "that signal was not
active this cycle", which is the one thing a teaching diagram must never
be ambiguous about.

Measured across the five models, by extracting every
`active_wires_.append("…")` from each `.cpp` and comparing with its
scene JSON:

| model | distinct names | JSON wires (named / unnamed) | appended with no wire | drawn, never lit |
|---|---|---|---|---|
| rvss | 21 | 22 (22 / 0) | **3** | 4 |
| nh-nf | 44 | 44 (44 / 0) | 0 | 0 |
| h-nf | 36 | 46 (19 / **27**) | **24** | 7 |
| nh-f | 43 | 53 (52 / 1) | 0 | 9 |
| h-f | 45 | 57 (57 / 0) | 0 | 12 |

**The three rvss names are fixed.** `RF_to_ALUMUX` was a case typo of the
drawing's `RF_to_ALUMux` - the neighbouring `Imm_to_ALUMUX` really is
spelled with the capitals, which is presumably how it happened - so the
register file's output into the ALU mux had never lit at all.
`IM_to_PC_pc` and `PC_to_IM_instruction` are deleted: all 22 wires in
that drawing are named, so neither could have referred to an unnamed
one, and the two wires they plausibly meant are already on the same
always-active list - `IM_to_RF_rs1_whole` and `PC_to_InstrMem`. Across
all eight scene files the closest names are `PC_to_IM` and `IM_to_P1`,
which are the **pipelined** drawings' names for those two wires.

**The gate is `test/wire_name_gate_test.cpp`.** It is a lint written as a
test, because that is where this project's gates live. It reads the
`.cpp` and the JSON from disk - no hand-copied name list - and checks its
own extraction: the number of `active_wires_.append(` occurrences must
equal the number of string-literal arguments it matched, so a name built
at run time would fail the gate rather than be quietly missed. It reads
the source rather than a running processor for two reasons: no single
program reaches every branch of `SetActiveWireNames`, so a run-time
union would under-report; and `kites_test` does not link the compiled
`.qrc`, so a resource path does not resolve there.

The drawn-but-never-lit set is asserted per model as an exact baseline
too. Those are not defects on their own - a drawing may carry wires the
highlighting does not model - but a wire that silently stops being lit,
or a new one nobody wired up, is now noticed.

h-nf's 24 unmatched names are recorded as a KNOWN set instead of being
fixed: see 94. No JSON was edited in this stage.

---

## 94. `H_NF_Processor.json` leaves 27 of its 46 wires unnamed, so two thirds of what h-nf computes lights nothing

`RV5StageProcessorHNF::SetActiveWireNames` appends 36 distinct names.
`H_NF_Processor.json` holds 46 wires of which **27 have an empty
`name`**, leaving 19 named. Twenty-four of the 36 names match nothing,
including backbone wires the model considers always active:
`PCMux_to_PC`, `P1_to_Control_RF_andall`, `RF_to_P2_UP`, `Imm_to_P2`,
`ALU_to_P3`, `HDU_to_P1` and eighteen more. The full list is in
`test/wire_name_gate_test.cpp`'s `kKnownUnmatchedHNf`.

The other four drawings are in much better shape: nh-nf and h-f name
every wire, nh-f leaves one unnamed.

So on the hazard-detection-without-forwarding model - the one a student
picks to watch interlocks - most of the diagram cannot light. The
processor is computing the right answer and sending it to a drawing that
has nowhere to put it.

**This is data work on the drawing, not a code fix**, which is why this
stage asserts the current set exactly rather than changing it: naming 27
wires means deciding which drawn line is which, and the only honest way
to do that is with the drawing in front of you. The gate's exact-set
assertion means progress on it is noticed - the test fails if the list
shrinks as well as if it grows.

There is no editor for these files in the tree, and the directory also
holds `H_NF_Processor copy.json`, `NH_NF_Processor - Copy.json` and an
unused `SSE.json`, so whoever picks this up should start by working out
which file is authoritative.

---

## 95. FIXED: the MEM/WB forwarding wires light one instruction too late - and so did most of the diagram

Separate from 81, and found by the test written for it.

`SetActiveWireNames` runs **after** the whole `Step()`
(`rv5s_processor_base.cpp:68-69`). By then `pipeline_decode` has already
reloaded `id_ex_reg_` with the instruction that will execute **next**
cycle, while the MEM/WB guard still reads `mem_wb_reg_.prev_*`
(`rv5s_processor_h_f.cpp:153`, `rv5s_processor_nh_f.cpp:147`). Those
`prev_` fields are a snapshot taken in `pipeline_memory`
(`rv5s_processor_base.cpp:440-445`) so that **execute** can see the
value that was written back in the same cycle - they are correct during
EX and one stage stale where this function actually runs.

Measured on h-f, union of `active_wires_` over every cycle, producer and
consumer separated by 0 to 3 `nop`s:

| nops between | does the machine forward? | `FDU_to_FMUX1_cntrl` | `FDU_to_fMUX2` | `FDU_to_FMUX3` |
|---|---|---|---|---|
| 0 | yes, EX/MEM | 1 | 1 | 1 |
| 1 | **yes, MEM/WB** | **0** | **0** | **0** |
| 2 | no, already written back | **1** | 0 | **1** |
| 3 | no | 0 | 0 | 0 |

So the MEM/WB half of the forwarding diagram is dark exactly when that
path is in use, and lit one instruction later when the value is already
architectural and nothing forwards. The `fMUX2` column is 0 at two nops
because that arm's rs1 branch appends only `P4EB_to_FWD_unit` and
`FDU_to_FMUX1_cntrl` (`rv5s_processor_h_f.cpp:155-159`).

Pinned, not fixed:
`ForwardingWires.TheMemWbGuardFiresOneInstructionTooLate` asserts the
current behaviour in both directions, so correcting it fails the test and
whoever does it finds this entry. The fix is presumably to read
`mem_wb_reg_` rather than `mem_wb_reg_.prev_*` in this function only,
leaving the datapath's use of `prev_*` alone - but that is a change to
what the diagram draws and wants its own evidence, which this stage did
not gather.

### The fix (`83aef6c`)

**The semantic, decided for this stage.** After each Step,
`active_wires_` describes exactly one cycle: the one whose instructions
the editor labels IF, ID, EX, MEM, WB. Those labels come from
`setProcessorState` (`rv5s_processor_base.cpp:158-165` as of this fix), so ID =
`if_id_reg_`, EX = `id_ex_reg_`, MEM = `ex_mem_reg_` and WB =
`mem_wb_reg_`. A conditional wire belonging to stage S is lit if and
only if the instruction labelled S uses it. The backbone is unchanged.

**The audit found far more than the MEM/WB arm.** `Step()` runs WB → MEM → EX → ID → IF.
- `pipeline_memory` copies `mem_wb` into `mem_wb.prev_*` before
  overwriting it (`base.cpp:517-527`).
- Each model's `pipeline_execute` does the same for `ex_mem.prev_*`
  (`h_f.cpp:418-437`, `h_nf.cpp:261-280`, `nh_f.cpp:360-379`,
  `nh_nf.cpp:254-273`).

So after the step:
- `ex_mem.prev_*` is the instruction labelled **WB**;
- `mem_wb.prev_*` is the one that has **already retired**.

Every condition that read a `prev_*` field was one generation stale:
- h_f's memory-read and writeback wires (`prev ‖ current`, so lit one
  cycle too long);
- nh_f's memory-read, writeback and branch wires (`prev` alone, so dark in
  the right cycle and lit in the next);
- all of nh_nf's MEM and WB wires;
- the MEM/WB forwarding arm on h_f and nh_f.

Three other groups were stale for the same underlying reason:
- `Imm_to_P2` and nh_f's `Control_to_P2_*` are **ID**-stage wires but read
  `id_ex_reg_`.
- The hazard-unit wires read `stall_fetch_and_decode_`, which says that
  the step **just run** stalled. They were therefore lit in the bubble's
  cycle, not in the cycle where the hazard is detected.
- nh_nf's branch wires read `ex_mem` and `mem_wb` (one stage late), and
  its `mem_wb` opcode test used an **8-bit** mask, `0b11111111`, which
  includes bit 7. A branch whose imm[11] is set never matched.

**What changed** (only `SetActiveWireNames`, plus const queries on `RV5StageVM_Base`):
- Every `prev_*` read now reads the current register. For the MEM/WB
  arm, `mem_wb_reg_`'s `instruction`, `reg_write` and `rd` are exactly
  what `pipeline_memory` copies into `prev_*` at the start of the next
  step, which is what EX then forwards from. The datapath's own use of
  `prev_*` is untouched.
- ID-stage wires decode `if_id_reg_` on a **fresh** `RV5SControlUnit`
  (`idStageControl()`). A NOP decodes to all false, as `pipeline_decode`
  treats it. `control_unit_` is never touched.
- The hazard-unit wires ask whether the **next** step stalls
  (`hazardUnitWillStall`). It is the same `check_data_hazard` call
  `Step()` makes, on the IF/ID, ID/EX and EX/MEM values those stages will
  have left by the time it runs. That covers a flush resetting IF/ID, and
  EX moving ID/EX into EX/MEM with the old fields as `prev_*`.
- `ALU_zerores_to_P3` is an EX wire, lit for a branch in EX whether or
  not it will be taken.
- The PC-mux wires follow what the mux selects:
  - the AND-gate path when a taken B-type is in MEM (the condition
    `pipeline_memory` redirects on);
  - PC+4 otherwise;
  - **neither** in a jump's EX cycle. A JAL/JALR redirects from EX, and no
    drawing has that path.

  On h_f, h_nf and nh_f, `ANDGate_lower_entry` stays grouped with the
  taken case, as the code had it. nh_nf keeps its own split (lower entry
  and target for any branch in MEM, the AND output for a taken one).

**Evidence:**
- `ForwardingWires.TheMemWbGuardFiresExactlyWhenTheMachineForwards`
  replaces the pinning test: the wires are lit at 0 and 1 nops, dark at 2
  and 3, on h_f and nh_f. The three tests that reached the MEM/WB arm at
  two nops now reach it at one.
- `test/wire_timing_test.cpp` has two tests:
  - **Trace:** add, lw, sw, a taken beq, a not-taken bne, jal and fadd.d,
    each alone in nops on h_f. Each cycle's conditional set is compared
    with a written per-stage rule table. It proves the wires follow this
    register-timing rule for those instructions on that model. It is the
    same code reading written down twice, so it cannot prove that the
    rule is the textbook's or that a name matches the line it is drawn
    as.
  - **Cross-checks:** on all four pipelined models and four programs, what
    the diagram shows after step k is compared with what the machine does
    in step k+1. The hazard-unit wires are lit ⇔ the PC holds. The AND
    output is lit ⇔ a misprediction is counted. `ALU1_to_PCMuxUp` is lit
    ⇔ no redirect. `mem_wb` after k equals `mem_wb.prev_*` after k+1.
- Mutation controls: restoring the old hazard-unit condition fails the
  cross-check, and restoring the old `ALU_zerores_to_P3` condition fails
  the trace.
- The processor is unchanged:
  - the action transcript md5 is identical
    (`a2c336b34fd64cf5b810af16a9603d66`);
  - livesweep (10,295 lines) and modelsweep (281,138 lines) are identical
    before and after;
  - the Undo-continue matrix is 0 differing over 6,550 cells (four
    programs × five models).

**What the code does that this stage recorded but did not change.** The
control unit sets `reg_write` for every OP-FP instruction, so h_f's
writeback wires light for `fadd.d`, an FP write. On h_nf most of this is
invisible anyway: 24 of its 36 names have no drawn wire (94).

---

## 96. FIXED: `RVSSProcessor::SetActiveWireNames` never truncates, so Undo and Redo leave stale wires lit

Every model is supposed to start each cycle from the constructor's
always-active backbone and add this cycle's wires to it. The four
pipelined models do - `rv5s_processor_h_f.cpp:87` resizes, and
`nh_nf.cpp:78`, `nh_f.cpp:74` and `h_nf.cpp:63` erase back to
`always_active_wires_count_`.

RVSS does not. Its clear is commented out:

```cpp
// first clear the current list
// TODO * we dont have to clear wire that will always be active
// active_wires_.clear();          // rvss_processor.cpp:69-72
```

Of its four callers only one compensates. `StepAndNotify` erases
**after** emitting (`rvss_processor.cpp:1215-1216`), which is why an
ordinary Run - whose loop is `StepAndNotify` - behaves. `DebugRun`
(`:1133`), `Undo` (`:1283`) and `Redo` (`:1351`) call
`SetActiveWireNames` and emit without erasing before or after.

Measured: on a loaded, stepped RVSS, calling `SetActiveWireNames()`
twice grows `active_wires_` the second time, while all four pipelined
models return the same size.
`WireNameGate.OnlyTheSingleCycleModelFailsToTruncateItsWireList` pins
exactly that.

The consequence for Undo and Redo is **read, not run**: this stage did
not drive the toolbar. From the code, each press adds this state's wires
again without removing the previous ones, so on the single-cycle model a
wire that was active once stays lit, and the list grows with every
press. `CircuitScene::updateCircuitState` does a `QList::contains` per
wire per update (`circuit_scene.cpp:170`), so the cost grows with it.

Not fixed here. The fix is one line - the same `erase` the four
pipelined models already do - but it changes what the diagram draws on
every Undo, and the right stage for that is the one that looks at the
circuit view with a window open.

### The fix (`68aaa3e`)

`RVSSProcessor::SetActiveWireNames` now erases back to
`always_active_wires_count_` at the top, as the pipelined models do. The
erase after the emit in `StepAndNotify` is still there and is now
redundant.

`test/rvss_wire_list_test.cpp` has one test per emitting path:
- StepAndNotify after an Undo;
- DebugRun, which must not keep a load's `Control_to_MemRead` lit in the
  next instruction's cycle;
- Undo twice;
- Redo twice.

All four failed before the change.
`WireNameGate.OnlyTheSingleCycleModelFailsToTruncateItsWireList` became
`EveryModelTruncatesItsWireList`.

Driving it showed something the read-only entry above could not: see 97.

---

## 97. RVSS Undo and Redo do not restore the control unit, so the single-cycle wires describe the wrong instruction

`RVSSProcessor::Undo` and `Redo` rewind the registers, the PC and
memory, and then call `SetActiveWireNames`
(`rvss_processor.cpp:1283, :1351`). That function reads only
`control_unit_`'s getters (`:88-112`), which neither path restores. After
an Undo, the conditional wires therefore show the last instruction
**decoded**, not the state undone to.

Measured, with a throwaway test against `68aaa3e`:
- program: `lw t0, 0(x0)` then `addi t1, x0, 2`;
- `Control_to_MemRead` is lit after the `lw` step and dark after the
  `addi` step, as it should be;
- it is **still dark after Undo** back to the `lw` state (pc 4).

Not fixed here. The pipelined models do not have this problem: their
wires read the pipeline registers, which Undo restores, and those
registers carry the control bits. The single-cycle model keeps no
per-step copy of its control signals.
Re-decoding the instruction at `program_counter_ - 4` on Undo/Redo, or
adding the control bits to the step delta, are both possible. Either one
changes what Undo draws, which wants its own stage.

---

## 98. Several "conditional" wires are also in the backbone, so their conditions never show

Each model's constructor appends an always-on backbone, and
`SetActiveWireNames` appends conditional names on top. Some names are in
both lists, so the drawing shows them lit every cycle whatever the
condition says:
- h_f, 7 of its 26 distinct conditional names: `P2_to_P3_MEMControl`,
  `P2_to_P3_WBcontrol`, `P3ALUres_to_DMup`, `RdP4_to_FU`,
  `P4EB_to_FWD_unit`, `P2_to_FDU_rs1`, `P2_to_FDU_RS2`;
- h_nf, 3 of 21: `P2_to_P3_MEMControl`, `P2_to_P3_WBcontrol`,
  `P3ALUres_to_DMup`;
- nh_f, 5 of 22: `P3ALUres_to_DMup`, `RdP4_to_FU`, `P4EB_to_FWD_unit`,
  `P2_to_FDU_rs1`, `P2_to_FDU_RS2`;
- nh_nf, 0 of 22.

The counts come from the same extraction as `wire_name_gate_test.cpp`.

So on h_f the EX-stage control wires into EX/MEM, and the forwarding
unit's inputs, look permanently busy. The forwarding tests already rely
on this: they assert only on the three FDU outputs because the inputs
are backbone (`forwarding_wire_test.cpp`,
`TheBackboneWiresAreOnWhateverTheGuardDecides`).

Not changed. Removing a name from the backbone changes what every cycle
draws, and it is a question about the drawing: should an idle control
wire be dark? That belongs with the frame review, not with the timing
fix.

---

## 99. Stopping a paused run makes the old processor clock once more, and that clock arrives queued after a processor switch

When a Run or Debug Run is stopped, `RV5StageVM_Base::Run` emits
`processorClockedSignal` once more as it leaves its loop
(`rv5s_processor_base.cpp:51-56`, "to get rid of any highlights"). That
emit happens on the **worker** thread. `ProcessorManager` receives it by
DirectConnection (`processor_manager.cpp:74-75`), so its slot runs there
too, and anything the slot emits towards the UI is **queued**.

The sequence on the toolbar path is Stop, then choose a processor:
1. The worker runs that last clock.
2. `changeProcessor` replaces the processor, and with it the circuit
   scene.
3. Only then does the UI thread drain the queue, carrying the old
   processor's state.

**Measured for the stage selection, fixed there.** The first version of
U4b's `selectedStagesSignal` handler applied the value it carried. The
test `StageHighlight.AProcessorChangeCarriesTheSelectionIntoTheNewScene`
saw the new scene receive the old processor's EX selection (`4`) after
the switch, where `0` was right. The handler now asks the manager for
its current selection when the event is delivered
(`main_window.cpp:201`), so a late delivery applies the state as it is
now.

**Not checked for the editor's stage labels.** `updateEditorHighlightSignal`
takes the same route with the same PCs. Whether that leaves the old
labels drawn after a switch was not run. It depends on what
`processor_state_` holds at that final emit, which this stage did not
measure.

---

## 100. The single-cycle stage cell shows the wrong instruction after Undo

U4b fills the circuit view's one CI cell for RVSS from
`current_instruction_` at `last_executed_pc_`
(`rvss_processor.cpp:66-71`). That is the instruction the wires describe.
`current_instruction_` is set only by `Fetch` (`:183`). `Undo` and `Redo`
(`:1228`, `:1297`) rewind the PC and registers but not that field or the
control unit.

So after an Undo the CI cell, like the wires (97), shows the last
instruction **fetched**, not the one undone to. This is the same root
cause as 97, and it would be fixed the same way: carry the decoded
instruction in the step delta.

Read, not run. The single-cycle test steps forward only
(`StageTable.SingleCycleShowsOneCellWithTheInstructionJustExecuted`).

---

## 101. FIXED: rvss's exit service ended the whole process

`RVSSProcessor::HandleSyscall`'s `SYSCALL_EXIT` case called `exit(0)`
(`rvss_processor.cpp:516` at base `19cf41f`). A program ending with
`li a7,10; ecall` on rvss therefore killed:
- the GUI;
- `kites-cli`, before it dumped its registers;
- the test binary. ctest then reported the test as **passed**: status 0,
  and no "FAILED" in the output.

Found while answering 66, because the plan was to make the pipelines
"behave like rvss", and that part of rvss could not be copied.

Fixed in `e2eeacf`. The service (now `ProcessorBase::ServeSyscall`)
prints the same two lines and sets the same `output_status_`, then calls
`StopForProgramExit()`. The program stops with its state intact, as 67
settled for nh-nf.

The ecall tests carry an `atexit` guard that turns a future `exit()`
during a run into status 1. The control that puts `exit(0)` back fails
35 of the 40 ecall rows.

---

## 102. FIXED: stepping onto an exit `ecall` deadlocked the UI thread

`ProcessorManager::step()` calls `StepAndNotify()` on the UI thread, and
`StepAndNotify()` holds `step_mutex_`. On rv5-nh-nf an ecall reached that
way called `RequestStop()`. `StopFromRunLoop()` declined, because no
Run() loop owns the thread. `StopAndReset()` then took `step_mutex_`
again (`processor_base.cpp:156` at base). A `QMutex` is not recursive,
so the thread deadlocked.

Measured at base: `SteppingOntoAnExitEndsTheProgramWithoutHanging/rv5-nh-nf`
hit its 10 s watchdog.

Fixed in `e2eeacf`/`03e4c9d`. `StopForProgramExit()`
(`processor_base.cpp:749`) only records the exit when it is not on the
run thread. Both `StepAndNotify()`s then return false
(`rv5s_processor_base.cpp:82`, `rvss_processor.cpp:1014`), and
`ProcessorManager::step()` takes its existing end-of-program path (67).

Control, routing the exit back through `RequestStop()`: all five
stepping rows hang at the watchdog.

---

## 103. FIXED: rv5s computed `auipc` (and so `la`) without the PC

The rv5s control unit sets `reg_write` for AUIPC but neither `alu_src`
nor any PC input (`rv5s_control_unit.cpp:54-57`). The ALU adds
`reg1_data` and `reg2_data`, which are whatever the immediate's bits
decode to as rs1/rs2. It never computes `pc + (imm << 12)`.

Measured on all four rv5s variants with
`la x10, w; ld x5, 0(x10)` (`w` a `.dword` in `.data`): x5 =
`0x0005051310000517`. Those are the program's own first two instruction
words (`auipc x10, 0x10000`; `addi x10, x10, 0`), i.e. the load read
address 0. rvss loads `0x1122334455667788`.

No test used `la` on a pipeline. E1's tests loaded the data base with
`li` (a single `lui`) instead.

### Fixed in E2 (`5791228`; tests `c08b87d`)

**Root cause.** The rv5s control unit gives AUIPC `reg_write` and
`alu_op 0`, and no `alu_src` and no PC operand
(`rv5s_control_unit.cpp:54-57`). Every variant feeds the ALU
`reg1_data` plus `reg2_data` (`h_nf.cpp:225`, `h_f.cpp:317`,
`nh_f.cpp:251`, `nh_nf.cpp:223`). For auipc those "registers" are
immediate bits 19:15 and 24:20, so the result was reg[x] + reg[y], and
0 in practice. Each variant already overrode the ALU for **lui** after
the fact; none did for auipc. All four shared the bug.

**The fix.** One helper, `RV5StageVM_Base::auipc_result`
(`rv5s_processor_base.h:130`), returns `pc + sext32(imm << 12)`. Each
variant applies it right after its lui override (`h_f.cpp:389`,
`h_nf.cpp:249`, `nh_f.cpp:343`, `nh_nf.cpp:252`). The writeback switch
already wrote AUIPC.

**Measured** with the `lirepro` harness, all five models:

| program | rvss | rv5s before | rv5s after |
|---|---|---|---|
| `nop x3; auipc x5,0; auipc x6,1; auipc x7,0xfffff` | 0xc, 0x1010, 0xfffffffffffff014 | 0, 0, 0 | same as rvss |
| `beq x0,x0,L; …; L: auipc x5,0` | 0x10 | 0 | 0x10 |
| `la x5, v` (`.data` +8), then `ld` | 0x10000008, 7 | 0x8, 0x1300000013 | 0x10000008, 7 on nh-f/h-nf/h-f |

On nh-nf, `la` stays wrong after the fix, by design. It expands to
`auipc` + `addi` with no gap between them, a chain nh-nf cannot run
(the same as a multi-instruction `li`, 77).

**Tests.** `test/pc_relative_test.cpp`, 44 tests, one per owed (case,
model). The expectations come from `~/atlas-verify/pcrel/gen.py`, out of
tree: address = 4·index, data label = `0x10000000` + offset, branch
outcomes from Python's own comparisons. Every test also asserts
agreement with rvss. The cases:
- auipc at three PCs, including a negative upper immediate;
- `la` of two `.data` labels;
- jal, jalr, j, jr and ret links and targets;
- all six branches, taken and not taken, forward and backward;
- jalr after an ALU or a load producer at distance 0, 1 and 2.

**Red** (tests only, `c08b87d`): 11 of 44 fail. Those are
`AuipcAtSeveralPcs`, `LaOfADataLabel` and `JumpsLinkAndLand` (whose
jr/jalr targets come from auipc) on the four pipelines. Branches and
jalr at every distance already passed.

**Control** (the fix reverted alone, out of tree): the same 11 names fail.

---

## 104. rv5-nh-nf drops a CSR instruction without latching EX/MEM

`RV5StageProcessorNHNF::pipeline_execute` returns early for any SYSTEM
instruction other than ecall/ebreak (`rv5s_processor_nh_nf.cpp:216-220`)
and calls `execute_csr()`, which is empty on every rv5s variant
(`rv5s_processor_base.h:124`).

Because it returns before latching EX/MEM, the previous EX/MEM contents
go through MEM again. The CSR instruction itself never reaches writeback
or retires.

Before 66, ecall took the same early return. That path is gone (66).
The CSR one is left as found.

### Measured in E2 (no code changed)

**What assembles.**
- Only `fflags`, `frm` and `fcsr` are CSR names
  (`registers.cpp:133-137`).
- `cycle` and `instret`, a numeric address (`csrrw x5, 0x003, x6`), and
  the pseudo-instructions `csrr` and `csrw` all fail to parse.
- So cycle and instret could not be measured at all.

**What runs.** The six forms on the three CSRs, padded. Each cell is rd
(the old value) / the read-back with `csrrs x7, csr, x0` / the CSR's
final value. Each program first writes a starting value: 6 for
`csrrs`/`csrrsi`, 7 for `csrrc`/`csrrci`.

| form | rvss (fflags, frm, fcsr alike) | rv5-nh-nf, nh-f, h-nf, h-f |
|---|---|---|
| `csrrw x5, c, x6` (x6 = 5) | 0 / 5 / 5 | 0 / 0 / 0 |
| `csrrs x5, c, x6` (from 6, x6 = 1) | 6 / 7 / 7 | 0 / 0 / 0 |
| `csrrc x5, c, x6` (from 7, x6 = 2) | 7 / 5 / 5 | 0 / 0 / 0 |
| `csrrwi x5, c, 5` | 0 / 5 / 5 | 0 / 0 / 0 |
| `csrrsi x5, c, 1` (from 6) | 6 / 7 / 7 | 0 / 0 / 0 |
| `csrrci x5, c, 2` (from 7) | 7 / 5 / 5 | 0 / 0 / 0 |

- **rvss** implements all six read-modify-write forms correctly on its
  three CSRs. They are three separate slots (72, point 1).
- **The four pipelines** execute every CSR instruction as a no-op. The
  CSR is never written and rd is never written. `execute_csr()` is
  empty (`rv5s_processor_base.h:124`).

**nh-nf's early return, traced.** The program is `addi x5,x0,1;
csrrw x0,fcsr,x0` (`00100293`, `00301073`), using the `lirepro` trace
mode:
- The csrrw never reaches MEM or WB.
- The `addi` sits in EX/MEM for two cycles (3 and 4) and passes WB
  twice (cycles 4 and 5).
- h-nf passes the csrrw down the pipe as a no-op.

The retired count is the same on both, 11, because the duplicate
retires in the csrrw's place. Architectural state is unchanged by the
duplicate (an idempotent re-write one cycle later). The stage view and
the per-instruction retire attribution are wrong.

Fix nothing here (E2's instruction). Still open.

---

## 105. The hazard unit stalls lui, auipc and jal on their immediate bits

`check_data_hazard` reads bits 19:15 and 24:20 of every instruction as
rs1 and rs2 (`rv5s_hdu.cpp:39-40`). For lui and auipc those are bits of
the 20-bit immediate, and for jal of the jump offset; none of the three
reads a register. It is the GPR half of 78, which recorded the FPR half.
`readsIntegerSource` is right for these three (false;
`instructions.cpp:1169-1189`), but the HDU never asks it.

**Measured** (`lirepro`, debug run, cycles). `lui x6, 0x28` and
`auipc x6, 0x28` have 5 in bits 19:15; `jal x0, 8` has 8 in bits 24:20.
Each is placed right after a producer of that register number ("hit") or
of x7 ("miss"):

| | rv5-h-nf hit / miss | rv5-h-f hit / miss |
|---|---|---|
| ALU producer, then lui | 22 / 20 | 20 / 20 |
| ALU producer, then auipc | 22 / 20 | 20 / 20 |
| ALU producer, then jal | 24 / 22 | 22 / 22 |
| `ld` producer, then lui | 22 / 20 | **21** / 20 |
| `ld` producer, then auipc | 22 / 20 | **21** / 20 |
| `ld` producer, then jal | 24 / 22 | **23** / 22 |

That is two false stall cycles on h-nf and one on h-f after a load. The
values are identical hit and miss (x6 = `0x28000` on all five), so this
is timing only.

On the forwarding variants the forwarding units also match those bits
(`nh_f.cpp:270-300`). The forwarded operand is then discarded: lui and
auipc override the ALU result, and jal's link and target do not use it.
No value changes. Not fixed: E2 records it.

---

## 106. rv5s executes `srai` with a shift of 32 or more as `srli`

RV64 `srai` keeps shamt[5] in bit 25. The rv5s ALU control tests
`funct7 == 0b0100000` (`rv5s_control_unit.cpp:187`). With shamt >= 32,
funct7 is `0b0100001`, the test fails, and the instruction runs as SRLI.
rvss is right; the Python oracle agrees with rvss.

Smallest repro, padded:

```
addi x6, x0, -1
srai x5, x6, 32      # rvss 0xFFFFFFFFFFFFFFFF; all four rv5s 0x00000000FFFFFFFF
```

The ISA sweep found 6 such programs per pipeline: shamt 32 and 63 with
x6 = -1, INT64_MIN and INT32_MIN. `srli` and `slli` are unaffected. `sraiw`
could not be tested (109). Not fixed in E2.

---

## 107. rvss rounds double-precision ops in whatever mode the last FP load left

Two defects compound:
1. **rvss never maps a dynamic rounding mode for doubles.**
   `RVSSProcessor::ExecuteDouble` passes the instruction's rm field
   through unchanged (`rvss_processor.cpp:378`). `ExecuteFloat` maps
   rm = 0b111 (DYN, the assembler's default) to `frm`, and so do all
   four pipelines for both widths.
2. **`Alu::dfpexecute` leaks the host rounding mode.** For an unknown rm
   (7 among them) it leaves the mode as it finds it (`alu.cpp:757-773`).
   Its `AluOp::ADD` case is the address add of `fld`/`fsd`, whose
   *funct3* (`0b011`) has just been applied as a rounding mode (RUP).
   That case returns before the mode is restored (`alu.cpp:780-787`),
   so the whole process stays in round-toward-+inf.

So on rvss, any double op with the default rm after an `fld` rounds up.
Measured, `fadd.d` of 1.0 and the smallest denormal:

| | rvss | rv5s (all four) | RNE (Python) |
|---|---|---|---|
| operands moved in with `fmv.d.x` | `0x3FF0000000000000` | same | `0x3FF0000000000000` |
| one `fld` executed first | **`0x3FF0000000000001`** | `0x3FF0000000000000` | `0x3FF0000000000000` |

The ISA sweep found this in 27 programs per pipeline across fadd.d,
fsub.d, fmul.d, fdiv.d, fsqrt.d and the D conversions. For example,
`fcvt.l.d` of -1.5 gives -1 on rvss and -2 on the pipelines; -2 is
correct for RNE.

The leak is process-wide: it changes the host's rounding mode for every
later floating-point calculation in the process, GUI included. Not
measured beyond the simulator. Not fixed in E2.

---

## 108. rvss does not write `fcsr` after a double-precision op; the pipelines do

`RVSSProcessor::ExecuteDouble` computes `fcsr_status` but never stores
it. Only `ExecuteFloat` calls `WriteCsr(0x003, …)`
(`rvss_processor.cpp:372`). The four pipelines write the status after D
ops as well.

So after any double-precision op that raises a flag, fcsr reads 0 on
rvss and 1 on the pipelines. The ISA sweep found 232 programs per
pipeline where fcsr differs; every one is a D op. It is the plumbing
that 72 describes, but 72 does not record that the models *disagree*. A
fix for 72 has to cover rvss's double path too. Not fixed in E2.

---

## 109. The base RV64 W instructions do not assemble

`addw subw sllw srlw sraw addiw slliw srliw sraiw` are in the
encoder's table (`instruction_string_map`, e.g. `instructions.cpp:203`
and `:238`) but not in `instruction_syntax_map` (`instructions.cpp:763`).
The parser looks the mnemonic up there with `operator[]`
(`parser.cpp:420-421`), gets an empty syntax list, and rejects every
operand form. `addw x5, x6, x7` fails to parse on every model. So do
the old names `fmv.s.x` and `fmv.x.s`.

The M-extension W forms (`mulw divw divuw remw remuw`) are in the syntax
map and work. `negw` and `sext.w` still work, because the pseudo path
emits `subw`/`addiw` without the parser (`pseudo_formats.cpp:415`).

Found by the ISA sweep: 433 of its 4935 programs could not be assembled.
Those are the 11 mnemonics above, plus `fclass.s`/`fclass.d`, which are
known (52). Not fixed in E2.

---

## 110. `li` of 0x7FFFFFFF loads 0xFFFFFFFF7FFFFFFF

`emitLi32` splits a 32-bit value as
`upper = (v + 2048) >> 12; lower = v - (upper << 12)`
(`pseudo_formats.cpp:182`). For `0x7FFFFFFF`, `upper` is `0x80000`.
`lui` sign-extends that to `0xFFFFFFFF80000000`, and `addi -1` leaves
`0xFFFFFFFF7FFFFFFF`. RV64 needs `addiw` here (or another split) for a
value whose rounding carries into bit 31.

Found by the oracle: the only oracle mismatch in 2144 programs that ran.
It is identical on rvss, nh-f, h-nf and h-f; nh-nf cannot run `li`'s chain
at all (77). Smallest repro: `li x5, 0x7fffffff`.

Not fixed in E2. Every value in [0x7FFFF800, 0x7FFFFFFF] is presumably
affected the same way; only 0x7FFFFFFF was run.

---

## 111. `call` and `tail` do not assemble, and `la` takes only `.data` labels

- **`call` and `tail`.** Both are listed as valid
  (`instructions.cpp:368-369`) and in `instruction_syntax_map`, but
  `pseudo_formats.cpp` has no arm for either. `call f` fails to parse on
  every model.
- **`la` of a `.text` label.** The parser rejects it with "Expected:
  Label defined in .data section" (`pseudo_formats.cpp:72-76`). That is
  deliberate in the code; RISC-V's `la` takes any symbol.

E2's control-flow tests use `jal ra, f` / `ret` in place of `call`, and
data labels for `la`. Not fixed in E2.

---

## 112. Undoing an ecall read does not give the input back, so stepping it again hangs

`ServeSyscall`'s read pops the line it consumed from `input_queue_`
(`processor_base.cpp:656`). Neither undo record has a slot for it:
`StepDelta` (`processor_base.h:64`) has none, and `RV5StageVM_Base::Undo`
(`rv5s_processor_base.cpp:987`) and `RVSSProcessor::Undo`
(`rvss_processor.cpp:1018`) restore registers, memory, the PC and the
pipeline, not the queue.

After Undo past a read, stepping forward again re-executes the read,
which finds the queue empty and waits forever (the untimed `input_cv_`
wait).

**Measured** on all five models (`test/ecall_undo_test.cpp`): with the
read's step inside the undone range, every step-k/undo-j/step-j
sequence hung at the test's 3 s watchdog. That is rvss from k = 21 and
the pipelines from k = 25, the step that serves the read. Those
sequences are now skipped and counted in the test: 21 on rvss and 25 on
each pipeline, with 110 and 138 others checked.

Everything else about Undo across an ecall holds:
- step to the end, then Undo to the start, restores the full state for
  print, read and exit on all five models;
- the E1 writeback flush and redirect replay exactly;
- an out-of-tree steprewind matrix over print, read and exit gives 15
  rows, 14,450 cells (Undo and Redo), 0 differing. The input is fed
  repeatedly there, which masks this entry on purpose.

**Not fixed:** the fix needs `StepDelta` to carry the consumed line and
both Undo implementations, and Redo, to put it back. All of that is
outside the code E1 added, which was E2's limit for this part.
