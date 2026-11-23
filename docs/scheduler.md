# Suggest reordering

Reordering scheduler by Nandan Nihal (CS25BTECH11014).

**Editor ribbon > Schedule > Suggest reordering** (also **File > Suggest
reordering**) looks for an order of your program's instructions that gives the
same result in fewer cycles on the selected processor, typically by moving an
independent instruction into a load-use stall. It changes nothing until you
press **Apply**, and one **Undo** takes the change back.

## What happens when you press it

1. **Schedule.** The scheduler (`src/scheduler/basic_block_scheduler.*`) splits
   the assembled program into basic blocks, builds the dependency graph of each
   block and list-schedules it, preferring instructions on the longest
   remaining dependency chain and filling stalls with independent ones.
2. **Apply at source level.** `src/scheduler/reorder_apply.*` turns the new
   order into new source text by moving whole lines (rules below). The result
   is re-assembled and must hold exactly the same instructions as before.
3. **Measure.** `src/scheduler_gate/reorder_gate.*` runs the original and the
   suggested program headless on the selected processor model, each capped at
   50,000 cycles. The suggestion is offered only if:
   - both runs halt;
   - final registers (x0-x31, f0-f31) and every byte of memory either run
     wrote are identical;
   - the suggested run takes **strictly fewer measured cycles**.

   Otherwise you see **No safe improvement found** and the reason.

The cycles shown are the measured Atlas cycles of those two runs, never the
scheduler's own estimate. (The estimate can be wrong: on `rv5-h-nf` it still
proposes moving a line in `examples/scheduler/load_use_reordered.s`, and the
measured runs show no gain, so nothing is offered.)

## Supported subset and limits

- **Processors.** Only the five-stage processors **with hazard detection**
  (with or without forwarding). On the single-cycle processor there are no
  stalls to remove. Without hazard detection, a dependency can read a stale
  operand by design, so a reordering can legitimately change the result. Both
  get no suggestion.
- **Instructions.** Only the integer **I and M** instructions are scheduled.
  Everything else is a barrier that keeps its place relative to every
  instruction in its block: floating point, CSR, `fence`, `ecall` and the
  like. Branches and jumps end a block.
- **Basic blocks only.** Nothing moves across a label, branch or jump. Labels
  stay where they are.
- **Memory.** All loads and stores keep their relative order. Addresses are
  not analysed.
- **Source-line level.** Only a line that assembles to exactly one instruction
  and defines no label can move. These stay fixed and act as barriers that
  nothing moves across:
  - labels;
  - directives;
  - comment-only lines;
  - lines that expand to several instructions: `la`, `call`, `li` with a large
    immediate, and so on.

  Blank lines stay where they are but are not barriers. A trailing comment
  moves with its instruction. Because of the barriers, a suggestion can be
  less ambitious than the scheduler's full order. It is still legal, since
  the lines of each run keep the scheduled order among themselves.
- **Custom pseudo-instructions** are not supported. A program that uses them
  gets no suggestion.
- **Input.** A program that reads input (the read syscall) is not run to
  completion, so it gets no suggestion.
- **Cycle cap.** A program that does not halt within 50,000 cycles gets no
  suggestion.

## Demos

`examples/scheduler/` holds the scheduler's Phase 1 demo programs and their
README. Measured on Atlas (pinned by `test/scheduler_differential_test.cpp`
and `test/scheduler_gate_test.cpp`):

| Program | rv5-h-f | rv5-h-nf |
| --- | ---: | ---: |
| `load_use_original.s` | 11 | 14 |
| `load_use_reordered.s` (hand-written) | 10 | 13 |
| `load_use_original.s` after Suggest reordering | 10 | 13 |

`anti_dependency_safe.s` (WAR) and `memory_order_safe.s` (load/store order)
are left unchanged.

## Tests

| File | What it pins |
| --- | --- |
| `test/scheduler_dependency_test.cpp` | Dependency edges, latencies and scheduled order for the demos. |
| `test/scheduler_differential_test.cpp` | Original vs reordered: identical final state, measured cycles, and two negative controls that must fail. |
| `test/scheduler_apply_test.cpp` | The source-line rules and the instruction-multiset check. |
| `test/scheduler_gate_test.cpp` | When a suggestion is and is not offered. |
| `test/reorder_ui_test.cpp` | The dialog, single-step Apply/Undo, the About dialog and where the action lives. |
