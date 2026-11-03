# ADSP APR Remote Intent ID Zero Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the captured remote GLINK INTENT `(size=128, id=0)` pass validation without weakening other intent checks.

**Architecture:** Remove only the remote-ID-nonzero condition from the production parser. A compiled fixture exercises the production helper with the captured bytes and a duplicate-zero negative case. Keep local intent IDs and all send/receive paths unchanged.

**Tech Stack:** XNU 12377 C patch series, compiled C helper harness, Python `unittest`, GitHub Actions kernel build, Fastboot RAM boot.

**Spec:** `docs/superpowers/specs/2026-11-03-adsp-apr-intent-id-zero-design.md`

## Global Constraints

- Allow zero only in received remote GLINK INTENT IDs.
- Keep remote channel IDs nonzero; preserve counts, frame lengths, size limits, and uniqueness checks.
- Keep AP-owned intent IDs 1–20 and the local TX_DATA response validator unchanged.
- Device testing must use `fastboot boot`; no partition or GPT writes.

## Review Focus

- Captured one-entry frame with ID 0 parses as one 128-byte intent: compiled fixture uses the literal wire values.
- Duplicate zero IDs remain rejected: compiled two-entry negative fixture.
- Zero-sized and greater-than-`0x4000` entries remain rejected: existing compiled cases.
- Zero remote channel remains rejected: existing compiled case.
- Patch replay includes 1158 and produces the same helper header as the staged source: ordered replay assertion.

---

### Task 1: Accept zero-valued remote intent IDs

**Files:**
- Modify: `tools/tests/test_adsp_apr_mode4.c`
- Modify: `.superpowers/work-1156/bsd/dev/beryllium_adsp_apr_probe.h`
- Create: `xnu/patches-12377/1158-beryllium-adsp-apr-intent-id-zero.patch`
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Modify: `NOTES.md`

**Interfaces:**
- Consumes: `beryllium_adsp_apr_parse_remote_intents(frame, frame_bytes, remote_channel_id, intents, intent_capacity, intent_count)`.
- Produces: the same parsed intent array/count, now including the valid remote ID 0.

- [x] Add a compiled test using the captured INTENT fields and a duplicate-zero list; run it against patches through 1157 and observe the expected assertion failure for ID 0.
- [x] Remove only the `id == 0` rejection from the helper; retain size bounds and duplicate detection. Generate patch 1158 from the staged 1157 baseline.
- [x] Extend helper compilation through 1158; verify 0 is accepted and duplicate zero is rejected. Patch application and staged-source byte comparison pass.
- [x] Run focused ADSP tests (42/42) and full `python3 -I -m unittest discover -s tools/tests -v` (45/45).
- [x] Commit and push patch, regression, design, plan, and handoff (`abae8c9d307f2e381a6913478e7fa19557af59ed`); dispatch CI run 41716939811/job 126199305412. Verify its final result and artifact next.
- [ ] Verify CI run 41716939811, job log, artifact checksums, and built kernel.
- [ ] Build payload with `.superpowers/ci/41489245547/beryllium.adt`, `tools/mkpayload.py`, and the `sweetadsp=4 rd=ufsroot0` arguments plus the verified ADSP/PAS marker ranges. Pack with `.superpowers/ci/41489245547/mkbootimg-local.py`, loader `.superpowers/ci/41489245547/enxerto-loader.kernel`, and `--base 0x10000000 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 --tags_offset 0x100 --pagesize 4096 --os_version 16.0.0 --os_patch_level 2025-06 --cmdline console=null`; 1157 round-trips byte-identically with these settings. Unpack 1158 and compare loader/payload byte-for-byte; assert no `ufsrw=1` / `rootrw=1`.
- [ ] Use `fastboot boot` only if the host sees an unlocked `beryllium` target. Capture whether APR progresses beyond the rejected INTENT and record mount/UI status without inferring from missing logs.
- [ ] Obtain one fresh whole-branch review, fix any Critical/Important findings with RED→GREEN coverage, then finalize the handoff.
