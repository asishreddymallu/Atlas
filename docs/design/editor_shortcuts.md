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

## Not done yet (Tier 2)

- `Ctrl+F` find, `F3` / `Shift+F3` find next / previous
- `Ctrl+H` replace
- `Ctrl+G` go to line
- auto-closing `(` and `"`
- bracket matching
- `Ctrl+D` (add selection to next occurrence)
- zoom
