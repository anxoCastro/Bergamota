# Beryllium Audio SMMU SID Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in `sweetadsp=6` diagnostic that reports the Apps SMMU route for Q6ASM stream `0x1821` without changing DSP or SMMU state.

**Architecture:** Handle mode 6 at the start of `beryllium_adsp_probe()`, before the existing mode validation and PAS/GLINK flags. Call the existing `sweet_smmu_probe_sid(0x1821, 0)` and return; this avoids ADSP firmware, PAS, GLINK, APR, AFE and context-bank inspection.

**Tech Stack:** XNU C patch series, Python `unittest` patch-replay checks, GitHub Actions XNU build, existing Fastboot RAM-boot workflow.

**Spec:** `docs/superpowers/specs/2026-11-04-adsp-smmu-audio-sid-probe-design.md`

## Global Constraints

- Mode 6 calls `sweet_smmu_probe_sid(0x1821, 0)` at level 0 only.
- Do not start PAS, load ADSP firmware, start GLINK/APR, configure AFE, or allocate PCM buffers in mode 6.
- Do not call `sweet_smmu_map_sid()` or write SMR, S2CR, context-bank or page-table state.
- Keep modes 1–5 unchanged.
- Hardware verification uses a temporary read-only image with `fastboot boot`; do not flash partitions.

## Review Focus

- Invalid or busy SMMU read: return/log `-1` without entering ADSP startup; exercise with a compiled function harness.
- No matching SMR: preserve the helper's `0` result; exercise classification forwarding in the harness.
- Existing translated, fault or mixed matches: report only; mode 6 must not adopt them; assert no map helper is available to the harness.
- Boot modes 1–5: retain their existing PAS/GLINK/APR flags; run the extracted function for each mode.
- Non-Beryllium device: preserve the board guard; run mode 6 with the board stub false and assert no SMMU call.

---

### Task 1: Add the standalone mode and regression check

**Files:**
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Create: `xnu/patches-12377/1167-beryllium-adsp-smmu-audio-sid-readonly.patch`
- Source staging: ignored `.superpowers/patch-work-1167/bsd/dev/beryllium_adsp.c`

**Interfaces:**
- Consumes: `int sweet_smmu_probe_sid(unsigned sid, unsigned level)` from `bsd/dev/sweet_smmu.c`.
- Produces: boot mode `sweetadsp=6`; one log line with SID `0x1821`, level `0`, and the helper result.

- [ ] **Step 1: Add a failing patch-level regression test**

Add `LINUX_SMMU_AUDIO_PROBE_PATCH`, extend the ordered driver-patch replay
through patch 1167, and add a test that compiles and runs the actual
`beryllium_adsp_probe()` body from the replayed driver. Stub only the board,
boot-arg, ADSP SMP2P and SMMU boundaries. Exercise modes 1–5 and assert their
current flags (mode 5 sets both APR and Linux AFE request flags); exercise
mode 6 with helper results `2`, `4`, and `-1` and
assert exactly one call with SID `0x1821`, level 0, no PAS/GLINK/APR flags and
no SMP2P probe. Exercise mode 6 on a non-Beryllium board and assert no SMMU
call. The harness must not define `sweet_smmu_map_sid()`, so an accidental
mapping call fails compilation/linking. Assert patch 1167 changes only
`bsd/dev/beryllium_adsp.c`.

- [ ] **Step 2: Run the focused test and confirm it fails**

Run: `python3 -I -m unittest discover -s tools/tests -p 'test_adsp_pas_patch.py' -v`

Expected: the compiled harness fails its mode-6 SMMU call assertion because
patch 1167 is absent; existing checks remain green.

- [ ] **Step 3: Generate patch 1167 from the exact 1131–1166 staged driver**

Apply the ordered ADSP patches 1131–1166 to an ignored staging tree, then add
the helper declaration and an early `mode == 6` branch in
`beryllium_adsp_probe()`. The branch logs the classification (`0` none, `1`
bypass, `2` translated, `3` fault, `4` mixed, `-1` unavailable/busy) and
returns immediately. Do not change the mode helper or existing mode branches.
Generate the context-bearing unified diff as `1167-beryllium-adsp-smmu-audio-sid-readonly.patch`.

- [ ] **Step 4: Run focused and patch-replay checks**

Run: `python3 -I -m unittest discover -s tools/tests -p 'test_adsp_pas_patch.py' -v`

Expected: all focused tests pass, including the new mode isolation check and
the test fixture's ordered replay through patch 1167.

- [ ] **Step 5: Commit the implementation**

Commit patch 1167 and the regression test with message
`feat(audio): add read-only audio SMMU probe`.

### Task 2: Build and observe the device result

**Files:**
- Modify: `NOTES.md`
- Artifact: ignored `.superpowers/ci/<run-id>/`

**Interfaces:**
- Consumes: committed patch 1167, existing CI workflow `xnu.yml`, and the
  Fastboot-connected Beryllium device.
- Produces: exact CI run/head SHA, kernel artifact checksum, and captured
  mode-6 classification.

- [ ] **Step 1: Compile locally if the host has the Apple-fork clang**

Run `tools/xnu-cc.py` for `bsd/dev/beryllium_adsp.c` using the successful CI
log and extracted build-tree. Expected: `0 of 1 files failed`. If this host
does not have the compiler, record that and use CI as the authoritative build.

- [ ] **Step 2: Dispatch and verify the kernel build**

After the in-progress 1166 run completes, dispatch `xnu.yml` on
`audio-poco-f1-adsp-bootstrap`. Confirm the run's head SHA includes patch
1167, the build job succeeds, `errors.txt` is empty, and artifact checksums
pass. Record run ID and kernel SHA-256 in `NOTES.md`.

- [ ] **Step 3: Boot mode 6 from RAM and capture logs**

Package a temporary read-only image using the verified artifact, set its
inner command line to `sweetadsp=6`, start the existing UDP log capture, and
run `fastboot boot <image>`. Expected log: one level-0 probe result for
`0x1821`, with no PAS, GLINK, APR, AFE, ASM, ADM or PCM startup. Leave the
device in Fastboot and preserve all partitions.

- [ ] **Step 4: Update the handoff and commit**

Record observed output separately from interpretation in `NOTES.md`. Commit
the handoff with the verified run, artifact checksum, and captured result.
