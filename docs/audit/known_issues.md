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
