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
