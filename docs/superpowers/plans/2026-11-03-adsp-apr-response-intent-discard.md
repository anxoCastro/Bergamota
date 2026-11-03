# ADSP APR Response INTENT Discard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent later peer INTENT advertisements from exhausting the persistent 64-entry list during the one-shot Q6Core response wait.

**Architecture:** Validate post-request GLINK INTENT frames into a bounded temporary array, consume only valid frames, and discard their parsed entries. Malformed frames still stop the response wait and emit a bounded diagnostic.

**Tech Stack:** XNU 12377 C patch series, Python `unittest` driver-contract tests, GitHub Actions kernel build, Fastboot RAM boot.

**Spec:** `docs/superpowers/specs/2026-11-03-adsp-apr-response-intent-discard-design.md`

## Global Constraints

- Do not change the single outgoing Q6Core `GET_FWK_VERSION` request.
- Keep malformed remote INTENT frames fail-closed and do not advance RX tail on parser failure.
- Keep RX_DONE, TX_DATA, and Q6Core response handling unchanged.
- Bound all diagnostic reads to the 528-byte local GLINK frame buffer.
- Device validation uses temporary `fastboot boot` only; do not flash partitions or alter GPT.

## Review Focus

- A stream of individually valid INTENT frames after the request must not fail at the persistent 64-entry limit: contract test requires discard rather than record.
- A malformed INTENT must not be consumed and its diagnostic must not read beyond `copy_bytes`: contract test checks validate-before-consume and bounded dump.
- RX_DONE and TX_DATA paths must remain present and unchanged in the response poller: existing response tests plus source comparison.

---

### Task 1: Discard validated post-request INTENT advertisements

**Files:**
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Modify: `.superpowers/work-1156/bsd/dev/beryllium_adsp.c` (staging source used to generate the XNU patch)
- Create: `xnu/patches-12377/1159-beryllium-adsp-apr-response-intent-discard.patch`
- Modify: `NOTES.md`

**Interfaces:**
- Consumes: `beryllium_adsp_apr_parse_remote_intents(frame, frame_bytes, remote_channel_id, intents, capacity, &count)` and `adsp_glink_rx_consume_frame(items, tail, frame_bytes)`.
- Produces: response polling that validates each later INTENT in temporary storage, consumes it only after validation, and does not append it to `state->remote_intents`.

- [x] Add `test_response_wait_discards_valid_remote_intents` to the driver-contract suite. Inspect `adsp_apr_poll_response`'s INTENT branch in `latest_driver`; assert the existing parser is used with a temporary array, persistent recorder is absent, consume follows successful parse, and persistent count is not mutated.
- [x] Run that one test against the 1158 patch series and observe failure because the current branch calls `adsp_apr_record_remote_intents`.
- [x] In the staging driver, replace only the response-wait INTENT handling: validate into a local array, log bounded raw bytes and retained count on parser failure, then consume and discard on success. Keep RX_DONE and TX_DATA handling unchanged.
- [x] Generate patch 1159 from the staging source baseline through patch 1158; extend ordered patch replay in `test_adsp_pas_patch.py` through 1159 and rerun the new test.
- [x] Run the focused ADSP test file and full `python3 -I -m unittest discover -s tools/tests -v`; inspect patch application and whitespace results.
- [ ] Commit and push the patch, regression, design, plan, and handoff. Dispatch GitHub Actions for the current 26.5 configuration and verify the pinned commit, job log, artifact checksums, and built kernel.
- [ ] Package a read-only RAM image with the previously validated loader and arguments; unpack and compare loader/payload and verify no `ufsrw=1` or `rootrw=1`.
- [ ] If Fastboot still reports one unlocked `beryllium` target, boot the image with `fastboot boot` only. Capture APR response progress and mount/UI state; do not infer a final state from absent log lines.
- [ ] Update `NOTES.md` with exact commit, CI IDs, artifact/image hashes, boot/capture result, and any remaining blocker.
