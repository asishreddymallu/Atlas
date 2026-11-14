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
