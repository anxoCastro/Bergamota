# Beryllium ADSP Preflight Root Hook Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans or superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep the opt-in SMP2P snapshot at UFS initialization and move firmware metadata reads until after XNU sets `rootvnode`.

**Architecture:** Split `beryllium_adsp_probe()` into an early, read-only SMEM snapshot and a root-dependent firmware metadata probe. Call the latter from `bsd_init()` immediately after `set_rootvnode()`; do not start PAS or change shared memory.

**Tech Stack:** XNU 12377 patch series, Python `unittest`, GitHub Actions kernel workflow, Fastboot temporary boot, UDP kernel log capture.

**Spec:** `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`, especially Startup flow and First-milestone acceptance.

## Global Constraints

- Firmware metadata reads are capped at 64 KiB and remain read-only.
- SMP2P item 443 is remote-owned; do not write it or allocate outbound item 429.
- Do not call PAS, assign the ADSP carve-out, start GLINK/APR, or touch LPASS.
- Keep the root read-only during device tests if the post-root hook can be observed; keep `sweetlog=0`.
- Device dumps and live logs stay under ignored `.superpowers/ci/` and out of Git.
- Device boot uses `fastboot boot`; do not flash partitions.

## Review Focus

- Missing `rootvnode`: firmware metadata probe must log and return without blocking iOS boot.
- Missing/short item 443: state remains `SMP2P_NOT_READY`; no item write follows.
- Boot without `sweetadsp=1`: neither early SMEM inspection nor firmware read runs.
- The firmware read hook runs strictly after `set_rootvnode()` in `bsd_init()`.
- The probe continues to avoid PAS, outbound SMEM, GLINK/APR, and LPASS operations.

---

### Task 1: Move the root-dependent probe after root mount

**Files:**
- Create: `tools/tests/test_adsp_patch.py`
- Modify: `xnu/patches-12377/1131-beryllium-adsp-apr.patch`
- Modify: `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`
- Modify: `NOTES.md`
- Test: `tools/tests/test_adsp_patch.py`

**Interfaces:**
- Consumes the unified diff in `xnu/patches-12377/1131-beryllium-adsp-apr.patch`.
- Requires the early hook to call only the SMEM snapshot and the later `bsd_init()` hook to follow `set_rootvnode()`.

- [x] **Step 1: Write the failing test** for the two call sites and assert the firmware reader is absent from the early hook.
- [x] **Step 2: Run it and confirm it fails** because the patch currently reads firmware in `sweet_ufs_init()` and has no post-root hook.
- [x] **Step 3: Split the implementation** so the early hook only snapshots item 443 and the firmware metadata hook runs after `set_rootvnode()`.
- [x] **Step 4: Run the test** and confirm both ordering assertions pass.
- [x] **Step 5: Review the patch diff** for no PAS, outbound SMEM, GLINK/APR, or LPASS calls; update the design and handoff notes.
- [x] **Step 6: Commit** the patch, test, and documentation.

### Task 2: Build and verify on the phone

**Files:**
- Modify: `NOTES.md`

- [ ] **Step 1: Run the focused test** with `python3 -m unittest discover -s tools/tests -v`.
- [ ] **Step 2: Build through GitHub Actions** and confirm the new patch applies and the kernel artifact is produced.
- [ ] **Step 3: Boot the rebuilt kernel temporarily** read-only with the existing Enxerto kernel/ADT, `sweetadsp=1`, `launchd_unsecure_cache=1`, and live UDP listener active.
- [ ] **Step 4: Verify the capture** reports the firmware probe after rootvnode is set, records its result, and does not show any PAS/GLINK/LPASS action.
- [ ] **Step 5: Commit and push** the completed patch, regression test, design update, and exact device evidence.
