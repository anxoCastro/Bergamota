# Beryllium SMMU CBAR lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Safely reuse the ADSP private SMMU context bank while leaving its canonical CBAR value configured, following the observed Qualcomm Linux teardown model.

**Architecture:** The helper will select only unused banks whose CBAR is zero or the exact audio value `0x1f300`. Teardown will not write CBAR back to zero; it will continue to disable the bank, flush translations, verify all other bank registers, restore and verify S2CR/SMR, and fail closed on any mismatch.

**Tech Stack:** C inline SMMU helper, C fake-MMIO harness, Python patch-series regressions, XNU GitHub Actions build, Fastboot RAM boot.

**Spec:** `docs/superpowers/specs/2026-11-06-beryllium-smmu-cbar-lifecycle.md`

## Global Constraints

- Preserve the fail-closed path on any uncertain translation state.
- Permit CBAR only when it is zero or exactly `0x1f300`.
- Do not relax SCTLR, TLB, TTBR, CBA2R, S2CR, or SMR validation.
- Device testing uses only temporary `fastboot boot`; never flash or write userdata/GPT.

## Review Focus

- A different nonzero CBAR must not be selected or silently preserved — test selection skips that bank.
- A sticky CBAR must not mask a stuck enabled context — test requires SCTLR.M to clear.
- CBA2R and page-table state must still be restored — test verifies those readbacks.
- Stream routing must not be restored before the bank is quiescent — test checks write order and final SMR/S2CR.
- Any failed safety readback must leave the mapping marked uncertain — retain existing failure regression and add a sticky-CBAR failure-injection check.

### Task 1: Pin the persistent-CBAR lifecycle in fake MMIO

**Files:**
- Modify: `tools/tests/test_sweet_smmu_audio_private.c`
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Modify: `xnu/patches-12377/1188-beryllium-smmu-cbar-lifecycle.patch`

**Interfaces:**
- Uses `sweet_smmu_audio_prepare`, `sweet_smmu_audio_install`, and `sweet_smmu_audio_restore_diagnostic` from the generated helper patch.
- The fake MMIO adds a sticky-CBAR behavior: accept writing `0x1f300` from zero; ignore a later write of zero; continue to honor writes to all other registers.

- [x] **Step 1: Add the red fake-MMIO regression**

  Add a test that prepares and installs a route with CBAR zero, enables sticky-CBAR behavior, restores after the required acknowledgements, and expects success with CBAR `0x1f300`, CBA2R zero, SCTLR.M clear, all other saved registers restored, and original S2CR/SMR restored. Add a check that a candidate with noncanonical CBAR is skipped.

- [x] **Step 2: Run only the focused C harness and confirm RED**

  Run the existing `test_sweet_smmu_audio_private.c` harness command from its Python test wrapper. Expected: sticky-CBAR teardown fails at CBAR readback; candidate-selection regression sees the noncanonical bank chosen.

- [x] **Step 3: Implement minimal helper changes**

  In the 1188 patch, define the canonical CBAR value once, filter candidates to zero/canonical CBAR, stop writing saved CBAR during teardown, and permit its persistent canonical value only after all remaining CB registers pass exact readback. Keep map and restore mismatch stages fail closed.

- [x] **Step 4: Run focused and repository regression suites**

  Run the focused ADSP/SMMU test file and the full `tools/tests` discovery. Expected: both pass; sticky CBAR is accepted, while stuck SCTLR/TTBR/TLB/S2CR/SMR cases continue to fail.

- [x] **Step 5: Commit the implementation and test**

  Commit as `fix(audio): follow Linux SMMU CBAR teardown` using the configured GitHub identity; do not add co-author trailers.

### Task 2: Build and validate the RAM-only device lifecycle

**Files:**
- Modify: `.github/workflows/xnu.yml` only if the normal workflow requires no source change; otherwise none.
- Modify: `NOTES.md`

- [x] **Step 1: Push the implementation and start the XNU Actions build**

  Record the run ID, exact commit SHA, workflow conclusion, kernel artifact ID and checksum.

- [x] **Step 2: Package a read-only mode-7 RAM image**

  Reuse the verified loader/ADT and read-only root arguments. Confirm `ufsrw=1` and `rootrw=1` are absent, and verify the packed kernel/payload hashes.

- [x] **Step 3: Confirm Fastboot target and boot temporarily**

  Require exactly one unlocked `beryllium`; run only `fastboot boot` and capture UDP/5140. No partition-write command is permitted.

- [x] **Step 4: Evaluate the hardware safety gates**

  Expected: if saved CBAR is zero, accept zero or canonical `0x1f300`; if saved CBAR is canonical, require it exactly. CBA2R and all other context state must match their saved values, SCTLR.M must read zero, and S2CR/SMR must restore exactly. The helper's success path reports route restoration and clears `mapping.uncertain`; preserve the retained-memory failure path on any mismatch. The device log confirms the success path but does not print numeric CBAR/CBA2R readbacks.

- [x] **Step 5: Record and commit the device result**

  Update `NOTES.md` with run IDs, hashes, the exact Fastboot result, capture checksum, observed restore gates and remaining limitations; commit and push with the user's GitHub identity.
