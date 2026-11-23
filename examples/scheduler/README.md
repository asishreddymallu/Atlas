# Phase 1 Scheduler Demos

These programs demonstrate the standalone basic-block scheduler's Phase 1 behaviour.

| Program | Demonstrates | Expected result |
| --- | --- | --- |
| `load_use_original.s` | Immediate `lw` to consumer dependency | A load-use stall occurs. |
| `load_use_reordered.s` | Valid static reordering | `addi x7, x0, 1` fills the load-use gap; registers match the original program with fewer cycles. |
| `anti_dependency_safe.s` | WAR dependency | The final write to `x2` must stay after `add`. |
| `memory_order_safe.s` | Load/store ordering | The load and store must not be reordered. |

For the load-use pair, run both files in Atlas with the five-stage processor and compare the cycle count. The automated scheduler tests verify identical final register values and these cycle-count improvements:

| Configuration | Original | Reordered |
| --- | ---: | ---: |
| Hazard detection + forwarding (`RV5StageProcessorHF`) | 11 | 10 |
| Hazard detection, no forwarding (`RV5StageProcessorHNF`) | 14 | 13 |

The no-hazard-detection processor modes are deliberately not used for correctness comparison: an unresolved dependency can execute with an incorrect operand in those teaching modes. Supporting them safely would require the scheduler to materialise explicit `nop` instructions, which is outside this reordering-only Phase 1 API.

The scheduler itself is intentionally not wired into the editor yet. It produces a safe suggested order; applying and visualising suggestions is Phase 2 work.
