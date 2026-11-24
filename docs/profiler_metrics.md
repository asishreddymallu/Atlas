# Profiler metrics

The first set of extra Profiler metrics (the 21 Nov roadmap). They are additive and read-only:
- the Profiler's existing counter table, its Instruction Type table and the `atlas.lab-record/1` format are unchanged;
- no simulator logic changed: nothing under `src/processor` or `src/assembler`.

Where they appear:
- **Profiler tab, "More metrics"**, below the two existing tables. Its tabs are Summary, By type, By mnemonic and Hottest loops, with a caption underneath.
- **Cache tab:** misses by cause, in each cache's live view.

Every number is labelled like the existing counters:
- **[Measured]**: counted by the simulator;
- **[Modelled]**: computed from other numbers, or from a model.

Counts are Measured; shares, rates and ratios are Modelled. A value the model cannot supply, or whose denominator is 0, reads **n/a**, never 0 or NaN.

## Where the numbers come from

Each cycle's execution-trace record names the instruction that retires in it: the WB occupant on a five-stage model, the single occupant on the single-cycle one. A collector reads every new record when the processor clocks, with the same direct connection the Profiler uses. So:
- the totals cover the whole run, not only the 100,000 cycles the trace keeps;
- Undo takes the undone cycles back off.

If cycles were ever missed (the trace had already dropped them), the totals' tooltips say they are incomplete. The calculators are Qt-free, in `src/profiler_metrics/` and `src/cache_lab/miss_classifier.*`.

## 1. Instruction mix (all five models)

| Number | Label | Definition |
| --- | --- | --- |
| Instructions retired (mix total) | Measured | Machine instructions retired so far. |
| Retired, per type (By type tab) | Measured | R, I, S, B, U, J, F, CSR and Other, by the machine instruction's type. |
| Retired, per mnemonic (By mnemonic tab) | Measured | Sorted by count, then by name. |
| Loads retired / Stores retired | Measured | Loads: `lb lbu lh lhu lw lwu ld flw fld`. Stores: `sb sh sw sd fsw fsd`. |
| Shares (type, mnemonic, loads, stores) | Modelled | Count / instructions retired. n/a until something has retired. |

**Not measured:** source lines. A pseudo-instruction counts as the machine instructions it expands to. For example, `la` counts one `auipc` and one `addi`.

The existing Instruction Type table instead files a pseudo line under the type of its last instruction. The two agree on every program in `examples/`. They differ only for a line like `la` that expands to instructions of different types (`ProfilerMetricsMix.AMixedTypePseudoLineIsCountedPerMachineInstruction`).

## 2. Branch statistics

| Number | Label | Definition | n/a when |
| --- | --- | --- | --- |
| Branches retired | Measured | B-type instructions retired (`beq bne blt bge bltu bgeu`). | |
| Taken branches | Measured | Branches after which the next instruction to retire is not at branch address + 4. | |
| Taken rate | Modelled | Taken / branches retired. | No branch retired. |
| Misprediction rate | Modelled | The model's own Branch mispredictions counter / branches retired. | The single-cycle processor, which keeps no such counter, or no branch retired. |

**Not measured:** `jal`/`jalr` (they are jumps, not branches).

A branch that is the last instruction to retire stays unresolved and is not counted as taken.

At the end of a program, mispredictions equal taken branches on the pipelines, and "Flushed cycles > taken branch" is 3 per taken branch. While a program is still running, the pipeline's counters can lead the taken count by the 1 or 2 taken branches in flight: a misprediction is counted when the branch resolves in MEM.

## 3. Speed-up over the single-cycle processor

| Number | Label | Definition |
| --- | --- | --- |
| Speed-up over single-cycle | Modelled | The program's cycles on the single-cycle processor / its cycles on the selected model, for example `0.55x`. On the single-cycle processor itself, `1.00x`. |

**Cycle counts only; a pipeline's advantage is a shorter clock period, which Atlas does not model.** The caption under the tables says this.

The single-cycle cycles come from running the same program headless on a fresh single-cycle processor, the way the reordering gate runs programs. It runs in small steps, so the window stays responsive.

**n/a when:**
- the program has not run to its end (while stepping);
- the replay is still running;
- the program waits for input;
- the replay does not end within 1,000,000 cycles.

## 4. Forwarding events (nh-f and h-f only)

| Number | Label | Definition |
| --- | --- | --- |
| Forwarded operands | Measured | Operand values the instruction in EX took from a later pipeline register instead of the register file. |
| Forwarded from EX/MEM | Measured | From the instruction one ahead. |
| Forwarded from MEM/WB | Measured | From the instruction two ahead, or from a load one ahead after its stall. |

**n/a** on the single-cycle processor, nh-nf and h-nf, which have no forwarding unit.

**Not measured:** whether the forwarded value was the right one. On nh-f a load-use consumer receives the load's address (known_issues 156), and is still counted.

## 5. Hottest loops (Hottest loops tab)

| Number | Label | Definition |
| --- | --- | --- |
| Loop (source lines) | | From a backward branch or `jal`'s target line to the branch's line. |
| Iterations | Measured | Times the target instruction executed. |
| Instructions executed | Measured | The per-line execution counts summed over the loop's lines. |
| Share | Modelled | Instructions executed / instructions retired. n/a until something retired. |

The table shows the top three by instructions executed.

**Ranked by instructions executed, not cycles: Atlas keeps no per-line cycle counts.** The caption and each row's tooltip say this.

- A loop is a backward B-type branch or `jal` (target at or before it) that has retired at least once.
- `jalr` targets are only known at run time and are not considered.
- Nested loops each appear, and an outer loop's lines include its inner loops'.
- A program with no backward branch shows no rows.

## 6. Misses by cause (Cache tab)

Each cache's live view (Instruction, L1, L2) shows, under the last access: `Misses by cause [Modelled]: compulsory N · capacity N · conflict N`.

| Cause | Definition |
| --- | --- |
| compulsory | The first access to that block since the cache's Reset. |
| capacity | Not compulsory, and a fully associative LRU cache with the same total number of lines and the same allocation policy, fed the same accesses, would also have missed. |
| conflict | Every other miss. This includes misses that the cache's own replacement order causes where LRU would have hit. |

- The three add up to the Misses counter.
- The classifier is fed from the live view's own per-access hook, so it does not depend on repaints.
- It starts again when the cache is reset or reconfigured.
- **Undo does not restore the cache's counters (known_issues 83), and these numbers follow the same limitation.**
- The line sits in the live-view pane rather than in the Statistics box: a row in the box would have made the window's minimum size larger.

**Not measured:** a cost in cycles, or which instruction caused the miss.

## How they are checked

| Check | Test |
| --- | --- |
| Hand-computed counts:<br>- load_use_original, stride_1, temporal_repeat;<br>- ex_mem_fwd and mem_wb_fwd forwards;<br>- stride and matmul loops;<br>- stride_1 and temporal_repeat miss causes. | `test/profiler_metrics_*_test.cpp`, `test/cache_miss_causes_test.cpp` |
| Mix agrees with the existing type table (every example, every model); shares sum to 100 %. | `ProfilerMetricsMix.*` |
| Pipeline counters agree with taken branches at the end of every example that ends. | `ProfilerMetricsBranch.*` |
| rvss cycles = retired instructions on every example that ends. | `ProfilerMetricsSpeedup.*` |
| Forwards equal the trace's own records; counting continues past the 100,000-cycle ring. | `ProfilerMetricsForwarding.*` |
| Causes add up to the misses on all 28 pinned lab rows; compulsory = distinct blocks; no conflict misses when fully associative. | `CacheMissCauses.*` |
| **Independent:** retired by mnemonic, loads, stores, branches, taken branches and the hottest loops, from the mini ISS on 13 programs × 5 models; the 3C split from the Python cache model on 9 programs × 7 configurations. | `verification/metrics/gen_metrics_cases.py` → `test/profiler_metrics_cases.inc`, `ProfilerMetricsOracle.*`, `CacheMissCauses.TheSplitMatchesThePythonModel` |

Forwarding and speed-up have no independent model, so they are checked by hand counts and by the trace's own records.
