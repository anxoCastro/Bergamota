# ADSP `apr_apps2` Peer OPEN Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `sweetadsp=4` consume one exact unhandled `apr_apps2` OPEN and continue the existing APR wait toward Q6Core.

**Architecture:** Add an exact, header-only OPEN validator and a one-time branch in `adsp_apr_open_channel`. The branch advances only the AP-owned RX tail and sends no ACK; it remains valid if the OPEN follows the `apr_audio_svc` peer OPEN. Extend the compiled helper harness, patch-contract tests, ordered patch series, and handoff evidence.

**Tech Stack:** XNU 12377 C patches, C11 helper harness, Python `unittest`, GitHub Actions KDK 26.5 build, Fastboot RAM boot.

**Spec:** `docs/superpowers/specs/2026-11-03-adsp-apr-apps2-peer-open-design.md`

## Global Constraints

- Keep the feature opt-in under the existing `sweetadsp=4` mode.
- Accept only one exact `apr_apps2` peer OPEN; send no ACK and do not implement its packet endpoint.
- Leave unknown, malformed, or duplicate peer OPENs rejected.
- Keep the Q6Core APR request unchanged and stop before audio data paths or codecs.
- Use `fastboot boot` only for device validation; do not flash partitions or GPT.

## Review Focus

- `apr_apps2` arrives after the desired channel opens — test the exact skip remains allowed then.
- A duplicate OPEN arrives — test that a second copy is rejected.
- Name length or NUL padding differs — test malformed frames are rejected.
- Command or remote ID differs — test command and ID mismatch are rejected.
- Unknown peer OPEN is present — keep the previous rejection behavior.

### Task 1: Add the exact `apr_apps2` frame validator

**Files:**
- Modify: `tools/tests/test_adsp_apr_mode4.c`
- Modify: ignored source `.superpowers/work-1156/bsd/dev/beryllium_adsp_apr_probe.h` at the 1155 baseline
- Create later: `xnu/patches-12377/1156-beryllium-adsp-apr-apps2-peer-open.patch`

**Interfaces:**
- Produces: `beryllium_adsp_apr_apps2_open_valid(const uint8_t *frame, size_t frame_bytes, uint16_t remote_channel_id)`.

- [x] Add assertions for the exact 24-byte OPEN, then command/ID mismatch, zero ID, wrong name/length, and truncation.
- [x] Compile the helper harness before adding the validator and confirm it fails because the validator is missing.
- [x] Add the exact constants and validator to the staged XNU header.
- [x] Recompile and run the harness; expect `APR mode-4 helper checks passed`.

### Task 2: Consume one `apr_apps2` OPEN after `apr_audio_svc`

**Files:**
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Modify: ignored source `.superpowers/work-1156/bsd/dev/beryllium_adsp.c` at the 1155 baseline
- Modify: `xnu/patches-12377/1156-beryllium-adsp-apr-apps2-peer-open.patch`

**Interfaces:**
- Consumes: `beryllium_adsp_apr_apps2_open_valid` from Task 1.
- Produces: one exact `apr_apps2` frame consumed without ACK while the APR open wait is active.

- [x] Add patch-contract assertions that fail before implementation: exact validator call, one-time state, no ACK send, no early-return, and no `!remote_open` guard on the `apr_apps2` exception.
- [x] Add the smallest driver branch; leave generic unknown/duplicate rejection unchanged.
- [x] Extend ordered patch replay through 1156 and compiled-helper replay through patch 1156.
- [x] Run focused ADSP tests and full `python3 -I -m unittest discover -s tools/tests -v`; expect all tests to pass.

### Task 3: Integrate, build, and validate the RAM image

**Files:**
- Modify: `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`
- Modify: `docs/superpowers/specs/2026-11-02-adsp-apr-q6core-mode4-design.md`
- Modify: `NOTES.md`
- Modify: generated patch `xnu/patches-12377/1156-beryllium-adsp-apr-apps2-peer-open.patch`

- [x] Generate patch 1156 from the staged 1155 source, apply it with `git apply --whitespace=error`, and byte-check it against the staged diff.
- [x] Verify ordered driver patch replay through 1156 and `git diff --check` for tracked source/docs/tests.
- [ ] Commit and push patch, tests, design updates, and handoff.
- [ ] Dispatch XNU workflow on `macos-26-intel`; verify success, patch 1156 in the log, `errors.txt` empty, and artifact checksums.
- [ ] Rebuild the existing read-only payload and header-v1 RAM image; unpack and byte-compare loader/payload.
- [ ] If one unlocked `beryllium` Fastboot target is available, verify product/unlocked immediately before `fastboot boot`, collect UDP/5140, and record whether the probe sends Q6Core and its response.
