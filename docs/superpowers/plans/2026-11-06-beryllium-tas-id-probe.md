# Beryllium TAS2559/TAS2560 Identity Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in XNU mode that safely reads the Poco F1 TAS2559 and TAS2560 identities over GPIO85/86 I2C.

**Architecture:** Extend the existing Beryllium ADSP diagnostic dispatch with isolated mode 8 and a finite GPIO bit-banged I2C probe. Reuse the project's TLMM open-drain conventions; keep ADSP modes 0-7 unchanged. After successful TLMM mapping, hold both TAS reset pins low on all exits; if mapping is unavailable, abort before release without claiming their state.

**Tech Stack:** XNU 12377 kernel C, AArch64 MMIO, existing unittest discovery and fake-MMIO harnesses, GitHub Actions KDK build, RAM-only Fastboot boot.

**Spec:** `docs/superpowers/specs/2026-11-06-beryllium-tas-id-probe-design.md`

## Global Constraints

- No partition, GPT, or userdata writes; test with temporary `fastboot boot` only.
- Require explicit `sweetadsp=8`; preserve modes 0-7.
- Do not program amplifier data registers, firmware, calibration, VBoost, or playback.
- Verify both active-low reset GPIO12/76 low before deassertion; abort before reset release if map/readback fails, and reassert low on every later exit.
- Use EAST TLMM windows `0x03555000` / `0x03556000` for GPIO85/86; confirm via tests.
- Open-drain I2C only; logical high means input/released. Clock-stretch wait <=1 ms.
- Keep proprietary TAS blobs and device dumps outside git.

## Review Focus

- Wrong TLMM region or per-GPIO offset: fake-MMIO test asserts exact GPIO85/86 addresses.
- Active drive-high on SDA/SCL: test checks every high transition disables OE without setting output-high.
- Reset left deasserted after NACK/timeout: patch checks assert cleanup follows every transfer outcome; device capture logs the final reset state.
- ID values are guessed or hidden: tests assert the raw TAS2559/TAS2560 register bytes are returned to the caller without hardcoded allowlists.
- Stuck SCL or SDA causes a hang: deterministic tests prove the 1 ms stretch and finite transfer bounds.

---

### Task 1: Pin down I2C protocol and identity behavior in regression fixtures

**Files:**
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Create: `tools/tests/test_adsp_tas_probe.c`

**Interfaces:**
- Consumes: existing host fake-MMIO patterns and patch replay utilities.
- Produces: an executable fake-GPIO test for the production I2C helper and static patch-stack assertions for mode 8 before production code changes.

- [x] **Step 1: Add `test_adsp_tas_probe.c`** to exercise the actual header API against a fake GPIO implementation; assert repeated-start frames, address/register bytes, read byte, ACK/NACK, and bounded clock-stretch timeout.
- [x] **Step 2: Extend `test_adsp_pas_patch.py`** to apply patch 1189 after the existing patch chain, compile/run the new C harness, and assert mode-8 opt-in, exact GPIO addresses, reset-low cleanup, no firmware/audio-power path, and unchanged modes 0-7.
- [x] **Step 3: Run the focused tests** against the current patch stack and confirm they fail because the helper and mode 8 are absent.
- [x] **Step 4: Commit the failing regression tests** locally before the production patch.

### Task 2: Implement the finite Beryllium TAS identity probe

**Files:**
- Create: `xnu/patches-12377/1189-beryllium-tas-identity-probe.patch`
- Modify: `tools/tests/test_adsp_pas_patch.py`

**Interfaces:**
- Consumes: `bsd/dev/beryllium_adsp.c` as produced by the existing cumulative patch chain, SDM845 TLMM layout, and the Task 1 fake-GPIO fixture.
- Produces: new `bsd/dev/beryllium_tas_id_probe.h` helper and `sweetadsp=8` dispatch; TAS2559 read of `0x4c/0x03`; TAS2560 read of `0x4d/0x7d`; bounded result log and reset-low cleanup after successful TLMM mapping.

- [x] **Step 1: Implement `beryllium_tas_i2c_read_reg(ops, context, address, reg, value)`** in the helper header with repeated-start I2C, open-drain callbacks, 1 ms maximum SCL stretch, and one bounded attempt per read.
- [x] **Step 2: Implement the XNU MMIO wrappers and mode 8 sequencing** in the same patch: force reset low, save SDA/SCL TLMM registers, validate/release lines, release reset only for ID reads, read both IDs, assert reset low on every exit, restore SDA/SCL configuration.
- [x] **Step 3: Return both raw identity bytes after ACKed reads;** reject NACK, timeout, and invalid line levels without assuming undocumented expected ID values.
- [x] **Step 4: Run the focused harness and patch replay**; verify modes 0-7 source behavior remains unchanged.
- [x] **Step 5: Commit patch and tests** with user-configured Git identity and no co-author trailer.

### Task 3: Build and package a read-only RAM image

**Files:**
- Modify: relevant GitHub workflow inputs only if needed; do not add proprietary firmware.
- Create: ignored `.superpowers/ci/<run-id>/` artifacts and logs.

**Interfaces:**
- Consumes: committed patch 1189 and test suite.
- Produces: successful KDK-enabled XNU artifact and a verified read-only Fastboot image using `sweetadsp=8`.

- [x] **Step 1: Run focused and full host tests** and verify patch replay before CI.
- [x] **Step 2: Trigger the XNU build with `gh`** and record the workflow/job IDs and exact build result in `NOTES.md`.
- [x] **Step 3: Verify kernel checksum and image header/payload**, confirm the image omits writable-root flags, and record hashes.

### Task 4: RAM-boot and inspect the identity result

**Files:**
- Create: ignored `.superpowers/ci/<run-id>/diagnostics/` capture only.
- Modify: `NOTES.md` and audio reference with the observed result.

**Interfaces:**
- Consumes: one unlocked Fastboot target whose product is `beryllium`, verified image, and the existing UDP diagnostic listener.
- Produces: exact boot result and bounded TAS identity log; resets remain low after the probe.

- [x] **Step 1: Preflight Fastboot** and stop if the target is absent or product/unlocked state is not confirmed.
- [ ] **Step 2: Run `fastboot boot` only** and capture the finite log; do not use `fastboot flash` or any userdata command.
- [ ] **Step 3: Record result and pending playback prerequisites** in `NOTES.md`; commit and push the completed milestone.

## Self-review

- Spec coverage: opt-in mode, exact board pins/registers, two ID reads, open-drain I2C, bounded waits, reset cleanup, no audio power path, CI build, and RAM-only boot are assigned above.
- Task interfaces align: Task 1 defines expected contracts, Task 2 implements them, Task 3 builds the exact mode, and Task 4 checks hardware behavior.
- Test coverage includes exact addresses, drive-high, raw ID reporting, NACK, timeout, stuck bus, and reset cleanup.
- No runtime image step can write Android partitions; the plan only uses `fastboot boot`.
