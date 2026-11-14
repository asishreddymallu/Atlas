# Editor keys — Tier 1

What the Editor tab's **Input** pane does with each key, whether Qt already
did it or this work added it, and the test that pins it.

The Output (disassembly) pane is read-only and is a different class
(`DisassemblyEditor`), so none of the implemented keys reaches it at all.
`EditorOutputPane.NoTextChangingKeyReachesIt` sends fifteen of them at it and
checks the text is untouched, and that Ctrl+A and Ctrl+C still work there.

The **Expanded** view is the same class as the Input pane but is set
read-only, so it gets the caret-moving keys and refuses the rest. That is one
guard, not two: every text-changing command checks `isReadOnly()` first
(`EditorKeysReadOnly.AReadOnlyPaneRefusesEveryTextChangingKey`).

## Where the code lives

| | |
|---|---|
| The transformations | `src/ui/editor_tab/editor_commands.h` / `.cpp` — pure functions of document text and a selection, returning one `Edit` (range to replace, replacement, new anchor, new position). No widget, no cursor, no clipboard |
| The keys | `Editor::handleEditingKey()` in `src/ui/editor_tab/main_editor.cpp` |
| Applying an `Edit` | `Editor::applyEdit()` — one `beginEditBlock`/`endEditBlock`, replacing only the named range, so one Ctrl+Z undoes one command |
| The indent unit | `Kites::editorTabWidth` in `src/ui/common/kites_editor.h` — the 4 that sets the tab stop distance and that one outdent removes from a space-indented line |

**No QAction and no QShortcut was added.** The window owns exactly seven key
sequences and `EditorShortcutSet.NothingNewBecameAnApplicationShortcut`
asserts that, plus zero `QShortcut` objects, plus that none of the nine new
sequences is owned by an action. Handling the keys in the widget is what
keeps that true. No `ShortcutOverride` handling is needed either: none of
F5, F6, F9, F10, Ctrl+O, Ctrl+S or Ctrl+Shift+M collides with any key below.

## Implemented

| Key | Behaviour | Test |
|---|---|---|
| `Home` / `Shift+Home` | To the first non-whitespace character; from there, or from inside the indentation, to column 0. Shift extends | `EditorCommandsSmartHome.*` |
| `Ctrl+C` (no selection) | Copies the whole line including its newline; the last line gets one added. Marked on the clipboard as a line copy | `EditorKeysClipboard.CopyAndCutWithNoSelectionTakeTheWholeLine` |
| `Ctrl+X` (no selection) | The same, and removes the line | same |
| `Ctrl+V` (no selection, line-copy clipboard) | Pastes the line **above** the caret's line; the caret stays on its own line. Any other clipboard pastes normally | `EditorKeysClipboard.PasteOfALineCopyGoesAboveAndLeavesTheCaretAlone` |
| `Alt+Up` / `Alt+Down` | Moves the touched lines one line, carrying the selection. Nothing at the first or last line | `EditorCommandsMoveLines.*` |
| `Shift+Alt+Up` / `Shift+Alt+Down` | Copies the touched lines above or below. Down moves the selection onto the new copy; up leaves it on the same line numbers | `EditorCommandsDuplicateLines.*` |
| `Ctrl+Shift+K` | Deletes the touched lines; the caret goes to the same column on the line below, clamped. On the last line, to the end of the one above | `EditorCommandsDeleteLines.*` |
| `Ctrl+Enter` / `Ctrl+Shift+Enter` | An empty line below / above, carrying the current line's indentation, caret at the end of it. `Key_Return` and `Key_Enter` both | `EditorCommandsInsertLine.*` |
| `Ctrl+L` | Selects the line including its newline; again, one line more | `EditorCommandsSelectLine.*` |
| `Enter` | Auto-indent: the new line copies the current line's leading whitespace, capped at the caret's column so pressing Enter inside the indentation does not deepen it. No extra indent after a label | `EditorCommandsNewline.*` |
| `Tab` (selection spanning lines) | Indents every touched line by one unit | `EditorCommandsIndent.*` |
| `Shift+Tab` (`Key_Backtab`) | Outdents the current line, or every touched line, by a tab or up to four spaces | `EditorCommandsOutdent.*` |
| `Ctrl+]` / `Ctrl+[` | Indent / outdent regardless of the selection | same two |
| `Ctrl+/` | Toggles `#` comments. Blank lines skipped; uncommented if **every** non-blank touched line already starts with `#`, otherwise `# ` goes in at the column of the least-indented non-blank line | `EditorCommandsToggleComment.*` |

**`Tab` with no selection, or with one inside a single line, is unchanged** —
it inserts a tab character, as it always has.
`EditorKeysWiring.TabWithNoSelectionStillInsertsATab` pins that.

## Qt native — pinned, not re-implemented

All in `EditorNativeKeys.*`.

| Key | |
|---|---|
| `Ctrl+Z`, `Ctrl+Y`, `Ctrl+Shift+Z` | undo, redo, redo. All three already worked; nothing was added |
| `Ctrl+A`, `Ctrl+C`, `Ctrl+X`, `Ctrl+V` | with a selection, the ordinary commands |
| `End`, `Ctrl+Home`, `Ctrl+End` | |
| `Ctrl+Left` / `Ctrl+Right`, with and without `Shift` | by word |
| `Ctrl+Backspace` / `Ctrl+Delete` | delete word |
| `Shift`+arrows, `PageUp`, `PageDown` | |

`Home` is **not** in this list. Qt's plain Home goes straight to column 0;
Smart Home replaces it.

## One existing behaviour moved

The completion popup guard in `Editor::keyPressEvent` ignored Enter, Escape,
Tab and Backtab by key, whatever the modifiers were, so `Ctrl+Enter` and
`Ctrl+Shift+Enter` went nowhere whenever a completion happened to be
showing. It now guards only the unmodified keys, which are the ones the
popup uses. `kites_test` links no instruction resources and so cannot open a
popup — the in-tree case skips and says so, and the check runs in the
out-of-tree `editorrecon` harness, which links them: with a popup open,
Ctrl+Enter inserts a line and a bare Enter still goes to the completer.

## Breakpoints

A breakpoint belongs to a line, and it follows that line. Each one carries a
`QTextCursor` selecting its whole line and the newline after it, which Qt
maintains through ordinary edits; the line commands apply as a single
replacement, inside which anchors collapse, so those remap through the
`EditorCommands::LineMap` each command returns. See
`docs/audit/known_issues.md` issue 18.

| Key / edit | What happens to a breakpoint | Test |
|---|---|---|
| typing inside a line | unchanged | `BreakpointKeysOrdinaryEdits.EnterBackspaceDeleteAndPaste` |
| `Enter` at column 0 | the line moves down with it | same |
| `Enter` mid-line or at the end | stays on the first part | same |
| `Backspace` / `Delete` joining two lines | the joined line keeps exactly one | same |
| deleting a multi-line selection | wholly removed lines lose theirs, a partly kept line keeps its own | same |
| pasting multi-line text | lines below move down | `BreakpointKeysOrdinaryEdits.PastingMultipleLinesShiftsWhatIsBelow` |
| `Alt+Up` / `Alt+Down` | the block and the line it jumped over swap, each carrying its own | `BreakpointKeysLineCommands.EveryCommandCarriesTheBreakpointsItShould` |
| `Shift+Alt+Up` / `Shift+Alt+Down` | the original keeps it, the copy gets none | same |
| `Ctrl+Shift+K` | removed with the line; the rest move up | same |
| `Ctrl+Enter` / `Ctrl+Shift+Enter` | lines shift; insert-above carries the current line down | same |
| `Ctrl+/`, `Tab`, `Shift+Tab`, `Ctrl+]`, `Ctrl+[` | unchanged | same |
| `Ctrl+X` on a whole line | removed with the line | `BreakpointKeysLineClipboard.CutRemovesItAndAPasteAboveShiftsLinesDown` |
| `Ctrl+V` of a line copy | lines below move down | same |
| `Ctrl+Z` / `Ctrl+Y` | left in range; one removed with its line does not come back | `BreakpointKeysUndo.UndoAndRedoLeaveBreakpointsInRange` |
| any key in a read-only pane | neither gained nor lost | `BreakpointKeysReadOnly.AReadOnlyPaneNeitherGainsNorLosesThem` |
| **File > Open** | all cleared — a breakpoint is a line number in the file that was open | `OpenClearsBreakpoints.OpeningAFileDropsThem` |
| setting the document text directly (`setPlainText`, `setRawText`) | the numbers are left exactly as they were | `BreakpointKeysDocumentSwap.LoadingAFileLeavesTheNumbersWhereTheyWere`, `OpenClearsBreakpoints.SetPlainTextOnItsOwnStillLeavesThemAlone` |

Cancelling the Open dialog clears nothing, because the clear sits inside the
`!filename.isEmpty()` branch of the handler. That is a structural guarantee,
not a tested one: `QFileDialog::getOpenFileName` is static and modal, so no
test can drive the cancel.
`OpenClearsBreakpoints.CancellingTheDialogClearsNothing` **documents the
contract and is not a guard** — no mutation of the implementation can make
it fail, and it is not counted as one anywhere. Cancel is covered by that
structure plus a manual check.

### In the Expanded pane

The Expanded pane shows the source with custom pseudo-instructions expanded,
so its line numbers are not the Input pane's.
`EditorTab::breakpointsForExpandedView()` reads the existing
expanded-line → original-line mapping the other way.

| Case | Where the dot goes | Test |
|---|---|---|
| a pseudo-instruction expanding to several lines | the **first** expanded line, once | `ExpandedBreakpoints.APseudoInstructionExpandingToSeveralLinesGetsOneDotOnTheFirst` |
| several expanded lines from one source line | still only the first | `ExpandedBreakpoints.OnlyTheFirstExpandedLineIsTakenEvenWhenSeveralMatch` |
| a source line that produces no expanded line (a comment, a blank) | no dot — there is nowhere to put it | `ExpandedBreakpoints.ABreakpointOnALineThatProducesNoExpandedLineGetsNoDot` |
| no pseudo-instructions, so nothing expands | the same line numbers as the Input pane | `ExpandedBreakpoints.WithNoMappingRecordedTheTwoPanesAgreeLineForLine`, `ExpandedBreakpoints.ShowingTheExpandedViewPutsTheDotsThereAndChangesNothingElse` |

The pane stays interaction-disabled, the gutter still toggles on the Input
pane only, and what `MainWindow` hands the VM is still the Input pane's
source lines.

Invariants, asserted after each of 200 random keys from a fixed seed in
`BreakpointKeysInvariants.TwoHundredRandomKeysKeepThemLegal`: never below
line 1, never past the end, never duplicated, and the count never grows.

The pure maps have their own table in `BreakpointMap*`, driven straight off
`EditorCommands::LineMap::mapped()`.

## Find, Replace and Go to line (Tier 2A)

The search itself is `src/ui/editor_tab/find_engine.{h,cpp}` — pure, text in
and offsets out, with its own table in `test/find_engine_test.cpp`. The bar
is `find_bar.{h,cpp}`, the go-to-line overlay is `goto_line_bar.{h,cpp}`,
and `Editor` wires them up. Both overlays are children of the editor's
**viewport**, in no layout, so the viewport is the same size open or closed.

Still no `QAction` and no `QShortcut`: the application owns exactly seven key
sequences and `FindShortcutSet.StillExactlySevenApplicationKeySequences`
asserts it.

| Key | Behaviour | Test |
|---|---|---|
| `Ctrl+F` | Opens the bar and focuses the field, selecting what is in it. Prefills from a selection, but never a multi-line one | `FindKeys.CtrlFOpensAndPrefillsFromASingleLineSelectionOnly` |
| `Enter` (find field) / `F3` | Next match, wrapping | `FindKeys.EnterAndF3WalkForwardAndBackwardAndWrap` |
| `Shift+Enter` / `Shift+F3` | Previous match, wrapping | same |
| `F3` with the bar closed | Opens it and goes to the next match for the last term; with no term, just opens | `FindKeys.F3WithTheBarClosedOpensItAndGoesOn` |
| `Esc` | Closes, returns focus to the editor, leaves the current match selected. In the editor it closes the bar too | `FindKeys.EscClosesLeavesTheMatchSelectedAndReturnsFocus` |
| `Alt+C` / `Alt+W` / `Alt+R` | Match case / whole word / regex, **while focus is in the bar** | `FindKeys.AltCWRToggleTheOptionsFromInsideTheBar` |
| `Ctrl+H` | Opens with the replace row, focus in the find field | `ReplaceKeys.CtrlHOpensTheReplaceRowWithFocusInTheFindField` |
| `Enter` (replace field) | Replace this match, then move on | `ReplaceOne.ReplacesTheCurrentMatchInOneUndoStepAndMovesOn` |
| `Ctrl+Alt+Enter` | Replace All, in one undo step | `ReplaceAll.IsExactlyOneUndoStepAndSaysHowMany` |
| `Ctrl+G` | Go to line. `12` or `12:5`; past the end goes to the last line | `GotoLine.AcceptsALineAndALineColonColumn`, `GotoLine.PastTheEndGoesToTheLastLineAndRubbishDoesNothing` |

`Alt+C`, `Alt+W` and `Alt+R` are handled **in the bar**, not in the editor,
because in the editor `Alt`+letter types that letter — measured on a bare
`KitesEditor` — and that behaviour is not this feature's to change.

### Behaviours worth stating

- **Finding never touches the undo stack.** Ten next/previous round trips
  and two re-searches leave `document()->revision()`, `isUndoAvailable()`
  and `isRedoAvailable()` exactly as they were
  (`FindIncremental.NoAmountOfFindingTouchesTheUndoStack`). The same for go
  to line (`GotoLine.NeverTouchesTheUndoStack`).
- **Incremental search runs from where the caret was when the bar opened**,
  not from the previous match, so the selection does not crawl forward as
  the pattern grows (`FindIncremental.TypingSelectsTheFirstMatchFromWhereTheBarWasOpened`).
- **Highlights compose, they do not replace.** They are a third list in
  `KitesEditor::refreshExtraSelections()`, under the diagnostics, so a
  squiggle is never masked and survives the bar closing
  (`FindHighlights.DiagnosticsSurviveDuringAndAfterASearch`). The execution
  highlight is painted in `Editor::paintEvent`, not an extra selection, so
  it composes without any help.
- **Only matches on screen get a selection**; the count is the whole
  document. 2000 matches hand Qt fewer than 200 formats and still read
  "1 of 2000" (`FindHighlights.OnlyVisibleMatchesGetASelectionButTheCountIsTheWholeDocument`).
- **Replace All is one undo step**, applied back to front so an earlier
  offset is never invalidated. `^` → `# ` comments every line, blank lines
  included (`ReplaceAll.CaretReplaceCommentsEveryLineAndTerminates`).
- **Replace is refused in a read-only pane** by the editor, not just by
  greyed buttons (`ReplaceReadOnly.AReadOnlyPaneRefusesToReplace`).
- **Only one overlay is open at a time**, in both directions
  (`GotoLine.EscClosesAndOnlyOneOverlayIsOpenAtATime`).
- **Breakpoints and a replacement that changes the line count**: a line a
  replacement splits keeps its breakpoint on the **last** of the lines that
  come out of it; lines below shift by the change in count; a breakpoint
  whose line is swallowed by a multi-line removal is dropped. This comes
  from the anchors, not a `LineMap` — Replace All is not an
  `EditorCommands` edit (`ReplaceBreakpoints.TheRuleWhenAReplacementChangesTheNumberOfLines`).
- **Esc with the completion popup open** goes to the popup, not the bar.
  `kites_test` links no `.qrc` and cannot open a popup, so that one is
  measured in the out-of-tree `findshots` harness.
- **Replace, Replace All and Next act on the current text**, not on the last
  search. The highlights are recomputed on a 150 ms debounce, which is right
  for repainting and wrong for acting: an edit followed immediately by any
  of the three would otherwise use offsets from before it. A dirty flag is
  set on every document change while the bar is open and those three
  re-search synchronously first; the debounce is untouched
  (`FindStaleMatches.ReplaceAllUsesTheCurrentTextNotTheLastSearch`,
  `FindStaleMatches.ReplaceOneUsesTheCurrentTextNotTheLastSearch`,
  `FindStaleMatches.NextUsesTheCurrentTextNotTheLastSearch`).
- **The match colours follow a theme change** while the bar is open. An
  extra selection keeps the format it was given, so the colours are rebuilt
  and the highlights repainted on both `themeChangedSignal` and
  `editorThemeChangedSignal`. **There is no in-tree test**: `kites_test`
  links no `.qrc` and so has no themes to switch between. It is a harness
  check — `findshots` reports the pixels of the new theme's wash on a
  non-current match before and after the switch.

### Limitations

Each is an entry in `docs/audit/known_issues.md`: a regex may match across
lines (21), the search stops at 10000 matches (22), "whole word" means
`[A-Za-z0-9_]` (23), the Output pane has no Find (24), and the breakpoint
rule above (25).

## Auto-closing pairs, bracket matching and zoom (Tier 2B)

The pair rules are pure functions beside the other commands in
`src/ui/editor_tab/editor_commands.h`; bracket matching is `bracketAt()` in
the same place; zoom lives in `src/ui/common/kites_editor.h` because the
**gutters** are there and a font change has to re-measure them.

Still no `QAction` and no `QShortcut`.

### Pairs

Only `(` `)`, `"` `"` and `'` `'`. **`[` `]` and `{` `}` are deliberately
not paired**: no RISC-V addressing mode or directive uses them, so closing
them would be noise. The rules are driven by the typed **text**, not
`Qt::Key`, so a layout where `(` is not Shift+9 behaves the same.

| Typing | What happens | Test |
|---|---|---|
| an opener where the next character is end of line, whitespace or `)` | the pair goes in, caret between | `EditorPairsOpening.AnOpenerClosesItselfOnlyWhereThereIsRoom` |
| an opener before a word character | just the character — this is what keeps `4(sp)` from becoming `4()sp)` | same |
| a quote after a word character | just the character, so `don't` stays one apostrophe | `EditorPairsOpening.AQuoteRefusesAfterAWordCharacterAndInAComment` |
| anything inside a `#` comment | no pair | same |
| `(` inside a `"…"` string | no pair | `EditorPairsOpening.ParenthesesDoNotPairInsideAString` |
| a closer where that closer is next | steps over it | `EditorPairsClosing.TypingTheCloserStepsOverIt` |
| an opener with a selection | wraps it, selection kept | `EditorPairsSelection.AnOpenerWrapsASelectionAndKeepsItSelected` |
| `Backspace` between an empty pair | both go | `EditorPairsBackspace.BetweenAnEmptyPairBothGo` |

`lw t0, 4(sp)`, `.asciz "a(b"` and `# it's` are typed character by character
in `EditorPairsTyping.TypingRealAssemblyCharacterByCharacter` — the only way
to catch a rule that is right alone and wrong in sequence.

**One `Ctrl+Z` undoes a whole typed pair**, and after a **type-over** it
undoes the pair in one step as well: the type-over inserts nothing, it only
moves the caret, so it puts nothing of its own on the undo stack
(`EditorPairsUndo.OneCtrlZUndoesAWholeTypedPairAndWhatATypeOverDoes`).

A read-only pane types nothing (`EditorPairsReadOnly.AReadOnlyPaneTypesNothingAtAll`).
With the completion popup open the pair keys are unaffected — the popup's
guard covers Enter, Escape, Tab and Backtab only — so pairs apply there too.

Breakpoints do not move for any single-character case; a **multi-line
selection wrap** carries a `LineMap` like every other command
(`EditorPairsBreakpoints.OnlyAMultiLineWrapCarriesALineMap`).

### Bracket matching

| | | Test |
|---|---|---|
| caret beside `(` or `)` | it and its partner are highlighted | `EditorBrackets.TheCaretBesideABracketFindsItsPartner` |
| no partner | that one alone, in the editor's error colour | `EditorBrackets.AnUnmatchedBracketHasNoPartner` |
| partner on another line | **not found, by design** | `EditorBrackets.TheSearchStopsAtTheEndOfTheLine` |
| brackets in comments or strings | ignored, on both ends | `EditorBrackets.CommentsAndStringsAreIgnoredOnBothEnds` |

Updated on `cursorPositionChanged`; when the caret is not beside a bracket
it costs one character comparison and sets nothing. It never touches the
undo stack. Text on the highlight measures 5.37 / 10.16 / 6.05 / 5.18 across
the four themes.

### Zoom

| Key | | Test |
|---|---|---|
| `Ctrl++`, `Ctrl+=` | one step larger | `EditorZoom.EachKeyMovesTheFontByOneStepAndCtrlZeroReturns` |
| `Ctrl+-` | one step smaller | same |
| `Ctrl+0` | back to the configured size | same |
| `Ctrl+wheel` | one step per notch | — (harness; see below) |

Own handling, not Qt's: `QPlainTextEdit::zoomIn()` and `zoomOut()` work on **point**
size and the editor font is set in **pixels**, and measured on a bare editor
none of `Ctrl++`, `Ctrl+=`, `Ctrl+-`, `Ctrl+0` or `Ctrl+wheel` did anything
at all. Handling it here also keeps the gutters in step, which Qt's would
not.

It is a **session delta**. The ceiling is `+18` steps; the floor is
whichever is higher, `-6` steps or the delta that reaches a rendered 5 pt,
because six steps down from a configured 8 pt would be 2 pt. At the
default 11 pt the range is **5 pt (7 px text, 9 px line) to 29 pt (39 px,
51 px)**, and the line numbers fit at every step of it. It never writes
the setting and **does not survive a restart**.
Applies to the Input, Expanded and Output panes — `zoomEnabled()` is opt-in,
so the profiler and compiler views are untouched. It works in a read-only
pane, because it changes nothing in the document
(`EditorZoom.AReadOnlyPaneZoomsToo`), and leaves the document revision, the
undo stack and the breakpoints alone
(`EditorZoom.NeitherTheDocumentNorTheBreakpointsMove`). The gutter follows
(`EditorZoom.TheGutterFollowsTheFont`) and both ends clamp
(`EditorZoom.ItClampsAtBothEnds`).

The find bar and the ribbon do not zoom. The bar does **re-fit** on every
zoom step, though: it is an overlay in a pane that can be narrower than
the row wants, so `FindBar::fitToWidth()` drops previous/next, then the
match count, then the input's minimum, then the toggles rarest first,
rather than letting Qt resolve the squeeze by overlapping them. Every
dropped button's key still works. Measured over width 849/1000/1200 x
replace hidden/shown x zoom 0/+18: 18 overlapping children before, 0
after (`FindBarGeometry.NothingSitsOnTopOfTheInputAtAnyWidth`,
`FindBarGeometry.WideningRestoresTheButtonsItDropped`).

## Not done yet (Tier 2B remainder)

- `Ctrl+D` (add selection to next occurrence) and multi-cursor
