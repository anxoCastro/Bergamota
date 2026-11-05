# Beryllium Linux-faithful Q6ASM finite-tone Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Keep the Poco in Fastboot and use RAM-only read-only images.

**Goal:** Add an opt-in finite Q6ASM/ADM PCM route that follows Linux MultiMedia1 and QUAT behavior, protects the TAS amplifiers in reset, and leaves the SMMU unchanged unless the test owns and can safely restore its private context.

**Architecture:** Patch 1168 adds Linux-compatible ASM/ADM encoders and validated response parsers. Patch 1169 adds a Beryllium-specific private SID `0x1821` stage-1 mapping for one contiguous 20 KiB mapped IOVA buffer (four 4,800-byte periods, rounded to five pages). Patch 1170 adds `sweetadsp=7`, asserts both TAS reset GPIOs, uses existing GLINK/APR and AFE helpers, sends four bounded PCM periods, and cleans up only after acknowledged commands. Uncertain DMA ownership is retained until reboot.

**Tech Stack:** XNU 26.5 patch series, C APR/SMMU helpers, compiled host C harnesses under `tools/tests`, GitHub Actions `xnu.yml`, and the existing temporary Enxerto/Fastboot workflow.

**Spec:** `docs/superpowers/specs/2026-11-05-beryllium-linux-q6asm-finite-tone-design.md`

## Global Constraints

- Patch 1168 must encode the Linux ABI exactly: MultiMedia1 DAI ID 0, ASM session 1 / DSP stream 1, pool 3, stream APR ports `0x101`, map token `0x100`, `0x10312` ADM topology, QUAT endpoint `0x1006`, logical ADM port 22, 48 kHz/S16/stereo, FL/FR map 1/2.
- ASM/ADM APR service/domain bytes remain zero, matching Linux's zero-initialized packets. Do not alter the validated AFE packet headers.
- Require exactly one Q6Core ASM service record 7/API 1.0 and one ADM record 8/API 2.0 before sending ASM/ADM commands.
- Patch 1169 may install a mapping only when a fresh probe finds no exact or masked `0x1821` SMR match and identifies an unreferenced disabled context bank plus invalid SMR. Never use `sweet_smmu_map_sid()` or CB3.
- Hold TAS reset GPIOs 12 and 76 low and verify both before starting the audio route. Never write TAS I2C, load amplifier firmware, or release reset.
- Bound the PCM run to four 4,800-byte writes, each individually completed before continuing.
- Restore SMMU registers only after validated ASM close, ASM unmap, ADM close and AFE stop. On uncertain completion, retain permanent memory and mapping until reboot.
- Preserve modes 1–6. Keep device testing read-only with `fastboot boot`; do not flash any partition, alter GPT or touch userdata.

## Review Focus

- APR request packet lengths and every field agree byte-for-byte with Linux source; response validation rejects wrong opcode/token/port/status and truncation.
- SMMU code rejects every masked/exact SID collision, counts all translating S2CR references, checks `SCTLR.M`, saves the selected state before writes, and never exposes the shared identity mapper.
- The five-page page-table walk maps only the contiguous physical buffer at IOVA `0x10000000`; each Q6ASM period gets `(IOVA + seq_id * 4800) | (1ULL << 32)`.
- GLINK intent reuse and APR event flow correctly handle ASM map response, ADM COPP response, basic results, and WRITE_DONE events.
- Every failure edge after a potentially accepted DMA command retains the backing buffer and SMMU context. Normal cleanup waits for a positive response at each stage.
- Mode 7 is isolated; existing boot modes and non-Beryllium board guard remain unchanged.

---

### Task 1: Add and test Linux ASM/ADM packet helpers (patch 1168)

**Files:**
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Modify: `tools/tests/test_adsp_apr_mode4.c`
- Create: `xnu/patches-12377/1168-beryllium-adsp-linux-asm-adm-packets.patch`
- Stage: ignored `.superpowers/patch-work-1168/`

- [x] Add failing C assertions for Linux packet bytes: ASM shared-map (40 B), OPEN_WRITE_V3 (36 B), PCM format update (44 B), ADM open (48 B), ADM matrix route (36 B), RUN_V2 (32 B), WRITE_V2 (52 B), ASM close (20 B), ASM unmap (24 B), and ADM close (20 B). Pin DAI ID 0, session 1, DSP stream 1, stream ports `0x101`, map token `0x100`, command token 1, zero ASM/ADM service-domain bytes, returned map handle/COPP ID parsing, WRITE_DONE address/sequence matching, and unique ASM 7/API 1 + ADM 8/API 2 service checks. Map size is `ALIGN(4 * 4800, 4096) = 20480` bytes.
- [x] Run the focused suite and capture failures for missing encoders/parsers.
- [x] Implement the packet helpers in `beryllium_adsp_apr_probe.h`; extend the existing response parsing to admit zero service/domain ASM/ADM replies while keeping AFE and Q6Core checks unchanged. Use tagged result structures for map handle, COPP open, basic result and WRITE_DONE.
- [x] Replay patches 1131–1168 in order, compile the actual helper header in the host C fixture, and run the focused and full repository test suites.
- [x] Commit as `feat(audio): add Linux ASM and ADM packet helpers` (`3fe9c34`).

### Task 2: Add owned per-buffer Apps SMMU translation (patch 1169)

**Files:**
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Create: `xnu/patches-12377/1169-beryllium-smmu-private-audio-buffer.patch`
- Source: `bsd/dev/sweet_smmu.c` in the staged XNU tree

- [x] Add a failing host C test for SMMU ownership selection: reject existing exact and masked matches, duplicate matches, no invalid SMR, an S2CR-referenced context bank, and a bank with `SCTLR.M=1`; accept only an unused disabled bank and preserve prior register values.
- [x] Add page-table tests for 4 KiB descriptors, IOVA-to-PA mappings across exactly five pages, unsupported size/range, and Q6ASM's `(1 << 32) | IOVA` encoding.
- [x] Implement serialized SMMU allocation/mapping for SID `0x1821`: re-read SMMU capabilities, allocate permanent zeroed table pages, map only the 20 KiB test buffer in a private 39-bit stage-1 context, save SMR/S2CR/CB state, and expose map/restore APIs. Recheck the live no-match precondition immediately before the first MMIO write.
- [x] Implement restoration so it refuses to run while a mapping is absent/unowned and restores the saved state only when mode 7 requests it after all DSP acknowledgements.
- [x] Run the actual helper logic through fake-MMIO tests and ordered patch replay. This host has no `clang`/`xcrun`, so local XNU compile was unavailable; GitHub build is pending.
- [x] Commit as `feat(audio): map the Q6ASM buffer in a private SMMU context` (`a814e6f`).

### Task 3: Add mode 7 TAS-safe bounded Q6ASM/ADM tone (patch 1170)

**Files:**
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Create: `xnu/patches-12377/1170-beryllium-adsp-linux-q6asm-finite-tone.patch`
- Stage: ignored `.superpowers/patch-work-1170/`

- [ ] Add failing mode/state-machine assertions before implementation: mode 7 requests PAS/GLINK/APR, stops on the first failed response, performs no more than four WRITE_V2 sends, rejects mismatched completions, and never requests TAS reset release. Keep mode 1–6 expected flags unchanged.
- [ ] Implement reset GPIO 12/76 assertion and output verification before the SMMU mapping; abort with no SMMU writes if either line is not confirmed low.
- [ ] Implement mode 7 using the tested helper sequence: private SMMU mapping, pool-3 map, ASM open, ADM QUAT COPP/matrix route, PCM format, existing AFE clock/config/start, RUN, four 25 ms WRITE_V2 cycles using four 4,800-byte offsets, then acknowledged close/unmap/ADM close/AFE stop.
- [ ] On timeout or malformed response after possible DSP acceptance, stop sending new PCM, keep TAS resets low, and retain permanent PCM/page-table memory plus active SMMU mapping; log the exact uncertain stage.
- [ ] Run patch replay, compiled mode/state-machine harness, focused/full tests, `git diff --check`, local C cross-compile where possible, and the GitHub Actions build. Record exact run ID, head SHA, kernel checksum and artifact status in `NOTES.md`.
- [ ] Package a temporary read-only image with `sweetadsp=7`; verify its inner payload/loader checksums; capture Fastboot RAM boot logs. Confirm every expected APR result, exactly four WRITE_DONE events and SMMU restore, then leave the phone in Fastboot.
- [ ] Commit the implementation and a separate handoff update with observed boot output and any remaining blocker.

### Task 4: Refresh the Linux audio reference and active handoff

**Files:**
- Modify: `docs/audio/BERYLLIUM_LINUX_AUDIO_REFERENCE.md`
- Modify: `NOTES.md`

- [ ] Replace the stale “SMMU still needs observation” statements with the verified mode-6 no-match, raw `sCR0`/ID values and explicit inference boundary.
- [ ] Record the Linux PCM/ASM/ADM packet and session contract with primary Linux source links.
- [ ] Record each patch/commit, CI run ID and head SHA, exact device result, capture checksum and current Fastboot state; distinguish observations from interpretations.
- [ ] Commit the documentation handoff.
