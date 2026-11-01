# Beryllium ADSP crash-reason timing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Record when global SMEM item 423 first appears or changes during the existing ADSP startup wait.

**Architecture:** Add a bounded, read-only item-423 reader to `beryllium_adsp.c`, reuse it for the current snapshots, and sample it from the existing 10 ms Q6V5 polling loop. Keep observations rate-limited to the first appearance and first text change.

**Tech Stack:** XNU 12377 kernel C, existing Bergamota patch series, GitHub Actions XNU build.

**Spec:** `docs/superpowers/specs/2026-11-01-beryllium-adsp-crash-reason-timing-design.md`

## Global Constraints

- The diagnostic is read-only and runs only in explicit `sweetadsp=2` mode.
- Do not write SMEM, issue PAS `SHUTDOWN`, or start GLINK/APR/audio before item 443 contains a valid `slave-kernel` state entry.
- Preserve Android and all partitions; use `fastboot boot` only.
- Do not add or run a test suite unless the user asks.

## Review Focus

- Missing/malformed item 423 must remain a non-fatal diagnostic result.
- Non-printable item bytes must be sanitized and the log buffer must remain bounded.
- Repeated unchanged samples must not flood the kernel log.
- Sampling must not mutate the item or the existing timeout/cleanup state.
- A text match must not be reported as proof of a watchdog event or root cause.

---

### Task 1: Sample the crash-reason item during Q6V5 startup

**Files:**
- Modify: `xnu/patches-12377/1139-beryllium-adsp-crash-reason-timing.patch`
- Modify: `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`
- Modify: `NOTES.md`

**Interfaces:**
- Reuse the existing private-SMEM item lookup for global host pair `0xfffe/0xfffe`, item `423`.
- Keep the existing five-second loop and its 10 ms delay.

- [ ] Refactor the current bounded item read into a helper that returns presence, item size, and a sanitized 128-byte text snapshot without logging unavailable samples.
- [ ] Sample once per existing loop iteration; log only first appearance and first subsequent content/size change with elapsed milliseconds.
- [ ] Keep the pre-PAS and timeout snapshots on the same helper and leave all writes/control operations unchanged.
- [ ] Validate patch application order and locally cross-compile the changed translation unit using the repository's XNU compile tooling; do not run the test suite.
- [ ] Dispatch the KDK-enabled XNU 26.5 GitHub Actions build and record the run ID, exact kernel hash, and build result.
- [ ] Prepare a temporary boot image and verify its round-trip. Use `fastboot boot` only if the connected device enumerates as `beryllium` in Fastboot; make no partition/GPT changes.
- [ ] Record the build/boot outcome and remaining blocker in `NOTES.md`; commit completed changes without co-author trailers.
