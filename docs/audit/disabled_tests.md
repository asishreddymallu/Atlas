> Historical record. Paths in this document are as of 2026-11-02; see [docs/ARCHITECTURE.md](../ARCHITECTURE.md) for the current layout and [docs/audit/path_mapping.md](path_mapping.md) for old to new paths.

# Disabled Tests Audit & Investigation

- **Branch**: `test/disabled-tests-audit`
- **Total Disabled Tests Investigated**: 18
- **Tests Re-enabled & Active**: 1 (`FIVE_STAGE_VM_TEST.no_hazard_no_forwarding_test_2` in `test/rv5s_test.cpp`)
- **Tests Remaining Disabled**: 17
- **Active Suite Count**: 159 tests (159/159 passing under CTest)

---

## Detailed Investigation Summary

### 1. `test/rv5s_test.cpp` (6 tests investigated)

| Test Name | Syntax | Result | Likely Reason Disabled | Proposed Fix |
| :--- | :--- | :--- | :--- | :--- |
| `FIVE_STAGE_VM_TEST.no_hazard_no_forwarding_test_1` | `// TEST` | Fails assertion | Assertion expects `x1 == 15`, but executing 12 `addi x1, x1, 10` instructions without hazard handling / forwarding produces `x1 == 40` due to pipeline stall/drain timings. | Recalculate the expected architectural register state under NHNF mode or update assembly program to match test assertion. |
| `FIVE_STAGE_VM_TEST.no_hazard_no_forwarding_test_2` | `// TEST` | **PASSES** (0.04s) | Was commented out along with the file. Passes assertion `x4 == 20` cleanly on `pipeline_test_code_2.s`. | **Re-enabled and active.** |
| `FIVE_STAGE_VM_TEST.hazard_no_forwarding_test_1` | `// TEST` | Fails assertion | Assertion expects `x1 == 36`, but executing 12 `addi x1, x1, 10` instructions under hazard detection with stalls produces `x1 == 120`. | Correct the expected assertion value to 120 (since stalls correctly serialize the 12 `addi` operations). |
| `FIVE_STAGE_VM_TEST.no_hazard_forwarding_test` | `// TEST` | Fails runtime | Throws `Failed to open file: ../examples/pipeline_test_code.s` (file does not exist on disk). | Provide `examples/pipeline_test_code.s` or update path to existing example. |
| `FIVE_STAGE_VM_TEST.harzard_no_forwarding_test` | `// TEST` | Fails runtime | Throws `Failed to open file: ../examples/pipeline_test_code.s` (file does not exist on disk). | Provide `examples/pipeline_test_code.s` or update path to existing example. |
| `FIVE_STAGE_VM_TEST.harzard_forwarding_test` | `// TEST` | Fails runtime | Throws `Failed to open file: ../examples/pipeline_test_code.s` (file does not exist on disk). | Provide `examples/pipeline_test_code.s` or update path to existing example. |

---

### 2. Superseded Pipeline Tests (3 tests investigated)

| Test Name | File & Lines | Syntax | Result | Likely Reason Disabled | Proposed Fix |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `RV5StageVM_H_F_TEST.hazard_forwarding_test_1` | `test/rv5s_h_f_test.cpp:6-17` | `// TEST` | Does not compile | Precedes `#include`s and `using namespace Kites;`; asserts `ReadGpr(6) == 15` on `pipeline_test_code_1.s` which only writes to `x1`. | Superseded by refactored tests in `rv5s_h_f_test.cpp`; remove draft or re-write using `runHazardProgram`. |
| `RV5StageVM_H_NF_TEST.hazard_forwarding_test_1` | `test/rv5s_h_nf_test.cpp:6-17` | `// TEST` | Does not compile | Precedes `#include`s and `using namespace Kites;`. | Superseded by `test1` (line 38) in the same file, which is active and passing. Remove duplicate draft. |
| `RV5S_NH_F_TEST.no_hazard_forwarding_test_1` | `test/rv5s_nh_f_test.cpp:7-18` | `// TEST` | Does not compile | Precedes `#include`s and `using namespace Kites;`. | Superseded by refactored tests in `rv5s_nh_f_test.cpp`. Remove duplicate draft. |

---

### 3. Cache Policy Tests (4 tests investigated)

| Test Name | File & Lines | Syntax | Result | Likely Reason Disabled | Proposed Fix |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `LRUTest.BasicEvictionOrder` | `test/cache_policies_test.cpp:148-170` | `#if 0` | Fails assertion | `cache.getHitCount()` is 0, expected 2. Eviction in 3-way LRU evicted lines unexpectedly. | Audit line replacement order and hit tracking for 3-way LRU configuration. |
| `LRUTest.AccessPromotesToMostRecent` | `test/cache_policies_test.cpp:173-199` | `#if 0` | Fails assertion | `cache.getHitCount()` is 2, expected 4. Re-access hit promotion did not prevent line eviction. | Audit LRU promotion logic on read hits. |
| `LRUTest.LRUAgeingAcrossManyCycles` | `test/cache_policies_test.cpp:282-313` | `#if 0` | Fails assertion | `cache.getHitCount()` is 20, expected 22. | Audit cycle-aging metric in LRU replacement policy engine. |
| `ReplacementPolicyTest.FIFOEvictsFirstInserted` | `test/cache_test.cpp:372-386` | `#if 0` | Fails assertion | `cache.getHitCount()` is 1, expected 2. Intermediate read of 0x00 touched way before eviction. | Align test expectation with 2-way FIFO hit semantics or fix FIFO queue index tracking. |

---

### 4. Custom Pseudo Instruction Tests (2 tests investigated)

| Test Name | File & Lines | Syntax | Result | Likely Reason Disabled | Proposed Fix |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `CustomPseudoManagerTest.AddPseudoInstructionAndExpandSingleLine` | `test/pseudo_inst_test.cpp:39-55` | `#if 0` | Does not compile | Calls `CustomPseudoManager::...` (capital C), but the declared namespace is `customPseudoManager` (lowercase c). | Change `CustomPseudoManager::` to `customPseudoManager::`. |
| `CustomPseudoManagerTest.ExpandPseudoInstructionWithMultipleExpansionLines` | `test/pseudo_inst_test.cpp:57-73` | `#if 0` | Does not compile | Calls `CustomPseudoManager::...` (capital C), but the declared namespace is `customPseudoManager` (lowercase c). | Change `CustomPseudoManager::` to `customPseudoManager::`. |

---

### 5. Floating Point Hazard Forwarding Tests (3 tests investigated)

| Test Name | File & Lines | Syntax | Result | Likely Reason Disabled | Proposed Fix |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `FExt_MixedHazard_H_F.mixed_gpr_fpr_hazard` | `test/rv5s_f_extension_test.cpp:329-334` | `#if 0` | Fails assertion | `ReadGpr(10)` is 0, expected 16. | Hazard forwarding unit in HF mode does not properly forward values between FP and integer registers. |
| `FExt_CompareHazard_H_F.compare_hazard` | `test/rv5s_f_extension_test.cpp:372-381` | `#if 0` | Fails assertion | `ReadGpr(13)` is 1, expected 0. | Hazard forwarding unit in HF mode does not handle FP compare-after-compute dependencies correctly. |
| `FExt_ConvertHazard_H_F.convert_hazard` | `test/rv5s_f_extension_test.cpp:500-506` | `#if 0` | Fails assertion | `ReadGpr(10)` is 0 (expected 24); `ReadGpr(11)` is 1 (expected 25). | Forwarding paths across format conversion instructions (`fcvt`) in HF mode need implementation. |
