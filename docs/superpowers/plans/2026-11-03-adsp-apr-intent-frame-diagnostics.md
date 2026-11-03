# ADSP APR Intent Frame Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make the next `sweetadsp=4` RAM boot report the complete bounded remote INTENT frame that currently stops the APR probe.

**Architecture:** Add diagnostic-only output to the existing failure branch in `adsp_apr_wait_remote_intent`. Log parsed header fields plus the bytes already copied into the 528-byte bounded buffer; preserve the existing rejection and RX-tail behavior.

**Tech Stack:** XNU 12377 C patch series, Python `unittest` patch-contract suite, GitHub Actions KDK 26.5 kernel build, Fastboot RAM image.

**Spec:** `docs/superpowers/specs/2026-11-03-adsp-apr-intent-frame-diagnostics-design.md`

## Global Constraints

- Keep `sweetadsp=4` behavior unchanged except for one bounded failure log.
- Dump no more than `ADSP_APR_GLINK_FRAME_MAX` (528) already-copied bytes.
- Preserve fail-closed behavior and do not consume a rejected RX frame.
- Do not change the INTENT parser or send Q6Core/audio traffic in patch 1157.
- Device validation uses `fastboot boot` only; no partition/GPT writes.

## Review Focus

- Malformed/truncated frame: log status and only the bytes actually copied; do not consume.
- Valid but parser-rejected INTENT: log all bounded bytes, including entries after the first.
- Unexpected command: preserve rejection while emitting enough header data to identify it.
- Maximum-size frame: never read or log past the 528-byte buffer.
- Logging path: do not alter RX head, TX metadata, or send ACK/control frames.

---

### Task 1: Add a fail-closed INTENT rejection diagnostic

**Files:**
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Modify: ignored staged driver `.superpowers/work-1156/bsd/dev/beryllium_adsp.c`
- Create: `xnu/patches-12377/1157-beryllium-adsp-apr-intent-frame-diagnostics.patch`
- Modify: `NOTES.md`

**Interfaces:**
- Consumes: `adsp_apr_wait_remote_intent()` and its bounded local `frame`, `copy_bytes`, `frame_bytes`, and `status` values.
- Produces: one rejection line with status, command, param1, param2, frame length, copied length, and the copied frame bytes in hex; no transport state change.

- [x] Add a patch-contract test requiring the complete diagnostic fields, a `copy_bytes`-bounded dump, and return-before-consume ordering.
- [x] Run the new test and confirm it fails against patch 1156 with the missing diagnostic contract.
- [x] Add the smallest diagnostic branch to the staged driver; do not modify parser semantics.
- [x] Generate patch 1157 and verify ordered patch replay plus `git apply --check --whitespace=error` and byte equality with the staged source diff.
- [x] Run focused ADSP tests (42/42) and full `python3 -I -m unittest discover -s tools/tests -v` (45/45).
- [x] Dispatch the fork's XNU kernel workflow with `gh`; verify successful job, patch 1157 in the log, empty `errors.txt`, artifact checksums, and kernel hash (`41706624115`, artifact `13268806811`).
- [ ] Rebuild and unpack the read-only header-v1 RAM image; compare the loader and payload byte-for-byte and confirm no write-enabled root arguments.
- [ ] If USB enumerates an unlocked `beryllium` Fastboot target, collect UDP/5140 and use `fastboot boot`; record the exact frame and whether mount/SpringBoard is visible. Otherwise finish the build/image handoff and leave device validation pending.
- [ ] Commit and push the tested patch, tests, design, plan, and exact handoff in `NOTES.md`.
