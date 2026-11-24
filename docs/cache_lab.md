# Cache Lab

The Cache tab is where the cache lab is done. It has three parts that go
beyond the configuration and statistics boxes:
- a **live view** of every cache line;
- **lab setup presets**;
- an exported **lab record** per run.

The programs are in `examples/cache_lab/` (see its README).

## Live cache-line view

Each sub-tab (Instruction Cache, L1 Cache, L2 Cache) shows, above the
existing per-line data table:

- **Lines by set and way**: one row per set, one column per way. A cell shows
  the line's tag in hex and its flags: `V` for valid, `D` for dirty (write
  back only). `-` is an empty way. Hover a cell to see where the line stands
  in the replacement order. "LRU order: 1 of 2 (replaced next)" means the
  policy would replace it next; FIFO counts by fill time instead.
- The line the **last access** touched is painted with the theme's cache-hit
  colour (a hit) or cache-miss colour (a miss). A store that misses with
  write no allocate touches no line, so nothing is painted.
- **Last access**: for example
  `READ 0x00010040 -> set 4, tag 0x4: MISS, evicted tag 0x3`.
- **Last 16 accesses**: the same lines, oldest first.

It is read-only: watching changes no counter, no replacement decision and no
data (`test/cache_live_view_test.cpp` runs stride_1 with and without it).

During a run the view repaints at most every 50 ms. After a manual step, an
undo, a load, a reconfigure or the end of a run it repaints at once. The
table only draws the rows on screen, so a cache of thousands of sets stays
responsive. Measured on `matmul_ijk.s` (RVSS, 1 ms step delay, 3 runs each):
16,779 ms without the live view, 16,762 ms with it.

How it stays safe: a Run executes on a worker thread, so the window never
reads the cache's lines during one. The cache keeps a passive record of its
last access (`Cache::lastAccess()`), and an observer
(`src/ui/cache_tab/cache_live_observer.*`) is told about every access on the
thread that made it. It copies that record and only the touched set into its
own snapshot, which the window reads.

## Lab setup presets

**Lab setup**, above the sub-tabs, sets the L1 cache to one of the lab's
setups in one click. It also sets the step delay to 1 ms and shows a
one-line description. The fields of the L1 sub-tab change as if you had set
them, and editing any of them afterwards returns the selector to
"None (set by hand)".

Exponents are the tab's own fields. **"2^n Lines" sets the number of sets**
(known issue 131), so the total line count is sets x ways.

| Preset | Lines / Ways / words-per-line | Size | Write hit / write miss | Replacement |
| --- | --- | --- | --- | --- |
| dm256 | 4 / 0 / 2 | 16 x 1 x 16 B = 256 B | Write Through / Write No Allocate | LRU |
| dm1024 | 6 / 0 / 2 | 64 x 1 x 16 B = 1 KB | Write Through / Write No Allocate | LRU |
| dm128 | 3 / 0 / 2 | 8 x 1 x 16 B = 128 B | Write Through / Write No Allocate | LRU |
| 2w128-fifo | 3 / 1 / 1 | 8 x 2 x 8 B = 128 B | Write Through / Write No Allocate | FIFO |
| dm256-32B | 3 / 0 / 3 | 8 x 1 x 32 B = 256 B | Write Through / Write No Allocate | LRU |
| 2w256-lru | 3 / 1 / 2 | 8 x 2 x 16 B = 256 B | Write Through / Write No Allocate | LRU |
| 2w256-fifo | 3 / 1 / 2 | 8 x 2 x 16 B = 256 B | Write Through / Write No Allocate | FIFO |
| 4w256-lru | 2 / 2 / 2 | 4 x 4 x 16 B = 256 B | Write Through / Write No Allocate | LRU |
| 2w256-lru-wb | 3 / 1 / 2 | 8 x 2 x 16 B = 256 B | Write Back / Write Allocate | LRU |
| 2w256-fifo-wb | 3 / 1 / 2 | 8 x 2 x 16 B = 256 B | Write Back / Write Allocate | FIFO |
| 4w512-wb | 2 / 2 / 3 | 4 x 4 x 32 B = 512 B | Write Back / Write Allocate | LRU |

dm256-32B is Lab 4's longer-line variant. 4w512-wb is the verification set.
The table lives in `src/cache_lab/lab_presets.cpp`, shared by the selector and
the tests.

## Measured results

L1 hits / misses / evictions, on RVSS (the L1 counts are the same on all five
processors), measured in the Cache tab and pinned by
`test/cache_lab_expected_test.cpp`. Every row matched the lab manual's
expected value, including the two setups the manual had only predicted
(marked *).

| Preset | Program | Hits | Misses | Evictions | Hit rate |
| --- | --- | ---: | ---: | ---: | ---: |
| dm256 | stride_1 | 768 | 256 | 240 | 75.00 % |
| dm256 | stride_2 | 256 | 256 | 240 | 50.00 % |
| dm256 | stride_4 | 0 | 256 | 240 | 0.00 % |
| dm256 | stride_8 | 0 | 128 | 120 | 0.00 % |
| dm256 | stride_16 | 0 | 64 | 60 | 0.00 % |
| dm256 | stride_32 | 0 | 32 | 30 | 0.00 % |
| dm256 | temporal_repeat | 240 | 16 | 0 | 93.75 % |
| dm256 | matmul_ijk | 853 | 363 | 155 | 70.15 % |
| dm256 | matmul_jik | 817 | 399 | 191 | 67.19 % |
| dm1024 | stride_1 | 768 | 256 | 192 | 75.00 % |
| dm1024 | stride_16 | 0 | 64 | 48 | 0.00 % |
| dm1024 | matmul_ijk | 992 | 224 | 0 | 81.58 % |
| dm1024 | matmul_jik | 992 | 224 | 0 | 81.58 % |
| dm128 * | temporal_repeat | 192 | 64 | 56 | 75.00 % |
| 2w128-fifo | temporal_repeat | 128 | 128 | 112 | 50.00 % |
| dm256-32B * | stride_1 | 896 | 128 | 120 | 87.50 % |
| dm256-32B * | stride_2 | 384 | 128 | 120 | 75.00 % |
| dm256-32B * | stride_4 | 128 | 128 | 120 | 50.00 % |
| dm256-32B * | stride_8 | 0 | 128 | 120 | 0.00 % |
| 2w256-lru | matmul_ijk | 827 | 389 | 181 | 68.01 % |
| 2w256-lru | matmul_jik | 756 | 460 | 252 | 62.17 % |
| 2w256-fifo | matmul_ijk | 788 | 428 | 220 | 64.80 % |
| 2w256-fifo | matmul_jik | 805 | 411 | 203 | 66.20 % |
| 4w256-lru | matmul_ijk | 691 | 525 | 317 | 56.83 % |
| 2w256-lru-wb | matmul_ijk | 886 | 330 | 314 | 72.86 % (94 write backs) |
| 2w256-fifo-wb | matmul_ijk | 888 | 328 | 312 | 73.03 % (94 write backs) |
| 2w256, `lru.lua` | matmul_ijk | 827 | 389 | 181 | 68.01 % |
| 2w256, `fifo.lua` | matmul_ijk | 788 | 428 | 220 | 64.80 % |

For dm256-32B on stride_2, 4 and 8 the manual predicted only the hits. The
misses and evictions shown are measured and not pinned.

## Lab record

**Export lab record...** (top right of the Cache tab) saves one JSON file per
run, schema `atlas.lab-record/1`, built by `buildLabRecordJson()` in
`src/cache_lab/lab_record.cpp`:

```json
{
  "schema": "atlas.lab-record/1",
  "atlasVersion": "1.0.0+abc1234",
  "timestamp": "2026-11-24T10:15:30Z",
  "program": { "name": "matmul_ijk.s", "sha256": "<64 hex digits>" },
  "processor": "Single-cycle",
  "preset": "2w256-lru",
  "caches": [
    {
      "level": "l1",
      "geometry": {
        "setsExponent": 3, "waysExponent": 1, "wordsPerLineExponent": 2,
        "sets": 8, "ways": 2, "lineBytes": 16, "totalBytes": 256
      },
      "policies": {
        "writeHit": "write-through", "writeMiss": "no-write-allocate",
        "replacement": "lru"
      },
      "results": {
        "accesses": 1216, "hits": 827, "misses": 389,
        "evictions": 181, "writeBacks": 0, "hitRate": 0.680099
      }
    }
  ],
  "cycles": 15117,
  "instructionsRetired": 15117,
  "completed": true
}
```

| Field | Meaning |
| --- | --- |
| `atlasVersion` | The Atlas version, as in Help > About: `1.0.0`, plus `+<short commit>` when the build was configured in a git checkout (`src/common/version.cpp`). |
| `timestamp` | When the record was exported, ISO 8601 UTC. |
| `program.name` | The opened file's name (`untitled.s` if none was opened). |
| `program.sha256` | SHA-256 of the source the last successful load assembled. |
| `processor` | The processor model, as the status bar names it. |
| `preset` | The lab preset L1 matches, or `null` if it was set by hand. |
| `caches` | `instruction`, `l1`, `l2`, in that order. `setsExponent` is the tab's "2^n Lines". `hitRate` is hits / accesses, 0 with no accesses. |
| `cycles`, `instructionsRetired` | The processor's counts. |
| `completed` | `true` only if the program ran to its end; a stopped, paused or never-run program gives `false`. |

The example's `instructionsRetired` equals `cycles` because the processor is
single-cycle.

## Tests

| File | What it checks |
| --- | --- |
| `test/cache_live_view_test.cpp` | The access record and snapshots, the observer, the set x way model and view, theme colours, and that observing changes no counter. |
| `test/cache_lab_presets_test.cpp` | The preset table, and that choosing one sets L1 and the step delay. |
| `test/cache_lab_expected_test.cpp` | The measured results above, in the real window. |
| `test/cache_lab_record_test.cpp` | The record schema, and matmul_ijk on 2w256-lru giving 827/389/181. |
| `test/cache_reference_check_test.cpp` | The live view against an independent LRU model (`test/cache_reference_model.h`), access by access, with negative controls. |

## Not done

Undo still does not restore the cache counters (known issue 83). Fixing it
means calling the cache's own undo history from every processor's step and
undo, which is a change to the simulator core and to the counters it reports
after an Undo. It is outside this read-only work.
