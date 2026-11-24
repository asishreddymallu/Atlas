# Verification results

| | |
| --- | --- |
| Commit | `287f25f17e42870f6164f113685135e3d7eefcbc` (branch `final/tidy`) |
| Atlas version | `1.0.0+287f25f` |
| Date | 2026-11-24 (run started 02:38 UTC) |
| Command | `scripts/run_verification.sh` (probes built fresh in `build-verify`) |
| Wall time | 1,004 s (about 17 min), including the probe build |
| Result | **VERIFICATION PASSED**: the summary matches `expected_summary.txt`, and 5 of 5 negative controls were caught |

"Before" is the last run of the same checker in the out-of-tree folder these scripts came from.

## The checkers

| Checker | Size | Result | Before | Verdict |
| --- | --- | --- | --- | --- |
| FP oracle self-test | 20,000 add/mul/div + 5,000 sqrt | `bad 0` | `bad 0` | pass |
| ISA sweep | 170 mnemonics, 140 swept, 15,328 programs × 5 models | 0 model divergences, 0 oracle mismatches, 5 expected assemble errors | the same; every program's pc, registers, fcsr and memory identical to the last run on all 5 models | pass, unchanged |
| Hazard / cycle sweep | 63,056 programs × 5 models | 0 assemble/run errors, 0 h-nf/h-f state divergences, 0 cycle mismatches vs the analytic model, 0 rvss oracle mismatches, 1 known nh-f contract case, nh-nf fails at k=3: 0 | the same (63,056 programs, same report) | pass, unchanged |
| `li` audit | 6,161 values | 0 wrong decoded, 0 wrong on rvss, h-f, h-nf, nh-f | 0 wrong | pass, unchanged |
| L1 cache sweep | 9 programs × 7 configs × 5 models = 315 runs | 12 mismatches, all L2 (see below); 0 on L1, I$ and every other L2 row | the same 12; all 945 result rows identical to the last run | pass, unchanged |
| Assembler corpus | 58 programs, 174 dumps | 0 differing, 0 missing, 0 unexpected | the same 174 dumps, byte-identical (once paths are made repo-relative) | pass, unchanged |
| Committed tables | 18 tables under `test/` | 18 matching | new check | pass |
| Negative controls | 5 | 5 caught | none existed | pass |

The five expected assemble errors are:
- `fmv.s.x` and `fmv.x.s`, which have no encoder entry;
- `slliw`, `sraiw` and `srliw` given shift amount 32, which is out of range for a W shift.

All five are rejected identically on every model.

### The 12 cache mismatches

They are known_issues 130, recorded when the sweep was written and still open:
- **Where:** L2 on rvss, for `matmul_ijk` and `matmul_jik`, under the six write-through/no-write-allocate L1 configurations.
- **The numbers:** the simulator counts 783 L2 misses where the textbook model counts 207.
- **Why:** a missed store is forwarded to L2 one byte at a time, so it counts as four L2 misses instead of one. 783 − 207 = 576 = 192 stores × 3.

The L1 rows, which are what the lab measures, all match. The sweep pins the 12 in `expected_summary.txt`, so a change in either direction fails the run.

### Negative controls

| Control | Problems before → after |
| --- | --- |
| ISA: one rvss register flipped (p0 x5) | 0 → 1 |
| Hazard: one h-f cycle count +1 (p0) | 0 → 1 |
| Cache: one L1 hit +1 (rvss, stride_1, dm256) | 12 → 13 |
| Assembler: one machine word changed (`ex_mem_fwd.s`) | 0 → 1 |
| Tables: one L1 hit changed in a copy of `test/cache_lab_cases.inc` | 0 → 1 |

## Step times (this run)

| Step | Time |
| --- | ---: |
| fpo-selftest | 2 s |
| isa-lists + gen | 0 s |
| isa-run | 95 s |
| isa-compare | 1 s |
| hazard-gen | 8 s |
| hazard-run | 703 s |
| hazard-compare | 22 s |
| li-values + li-check | 1 s |
| li-run | 76 s |
| cache | 37 s |
| asm | 6 s |
| tables | 1 s |
| negative-controls | 46 s |

The full summary of this run is `expected_summary.txt`.
