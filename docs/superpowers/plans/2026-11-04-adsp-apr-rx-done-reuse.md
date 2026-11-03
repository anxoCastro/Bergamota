# ADSP APR RX_DONE_W_REUSE Implementation Plan

> **For agentic workers:** Use this plan task by task. Steps use checkbox syntax for tracking.

**Goal:** Let the one-shot APR response wait consume a valid GLINK `RX_DONE_W_REUSE` acknowledgement for its Q6Core request.

**Architecture:** Add command 14 as an eight-byte GLINK control frame and route it through the existing one-shot request completion checks, validating the remote channel and outstanding intent ID. Preserve the remote intent and stop on any mismatch or duplicate.

**Tech Stack:** XNU 12377 C patch series, Python `unittest` contract checks, compiled C helper fixture, GitHub Actions kernel build, Fastboot RAM boot.

**Spec:** `docs/superpowers/specs/2026-11-04-adsp-apr-rx-done-reuse-design.md`

## Global Constraints

- Keep one outgoing Q6Core `GET_FWK_VERSION` request.
- Consume command 14 only after exact channel and intent matching.
- Do not remove the reusable remote intent from the retained table.
- Do not change command 6, TX_DATA, APR response parsing, or other GLINK commands.
- Device validation uses `fastboot boot` only; do not write partitions or GPT.

## Review Focus

- Command 14 must be exactly an eight-byte frame; seven bytes remain incomplete.
- The response wait must reject wrong channel, wrong intent ID, and duplicate completion before consuming RX data.
- A successful reuse acknowledgement must not retire the remote intent or trigger another APR request.

---

### Task 1: Accept the matching reuse acknowledgement

**Files:**
- Modify: `tools/tests/test_adsp_apr_mode4.c`
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Modify: staged `bsd/dev/beryllium_adsp.c` and `bsd/dev/beryllium_adsp_apr_probe.h`
- Create: `xnu/patches-12377/1160-beryllium-adsp-apr-rx-done-reuse.patch`
- Modify: `NOTES.md`

**Interfaces:**
- `beryllium_adsp_apr_glink_frame_bytes(frame, available, &frame_bytes)` recognizes command 14 as an eight-byte control frame.
- `adsp_apr_poll_response()` validates and consumes command 14 through the existing outstanding-request completion path.

- [x] Add a compiled literal command-14 frame fixture; assert seven bytes are incomplete and eight bytes are complete.
- [x] Add a response-wait contract check requiring both command values 6 and 14 to share channel, intent, duplicate, and consume checks.
- [x] Run the focused checks against patches through 1159 and observe expected failures.
- [x] Add `BERYLLIUM_ADSP_GLINK_CMD_RX_DONE_W_REUSE = 14`, recognize it in frame-length parsing, and route it through the strict completion path with a distinct diagnostic.
- [x] Replay patches 1131–1160, ensure the staged source matches, and run focused plus full tests (44/44 focused, 47/47 full; exact apply and source comparison passed).
- [x] Commit and push the patch, tests, spec, plan, and handoff; dispatch the XNU 26.5 GitHub Actions build (`0147b720203facee4164f6ecfec5a5bca723c8bb`, run 41738200069).
- [ ] Verify the pinned CI commit and kernel artifact, then package a read-only image with `sweetarm64e=1` and the existing ADSP 4 arguments.
- [ ] RAM-boot the image and capture whether the matching reuse acknowledgement advances to a validated Q6Core response; also record mount/UI state without inferring from missing log lines.
- [ ] Update `NOTES.md` with exact patch/commit, CI and artifact IDs, image/capture hashes, results, and remaining blocker. (Current code/test and prior retry-boot handoff recorded; add CI and patch-1160 boot evidence after those milestones.)
