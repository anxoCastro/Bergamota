# ADSP APR v1 response-header support implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans or superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse the Poco's observed APR v1 Q6Core firmware-version response without weakening packet or body-length validation.

**Architecture:** Decode the APR header-field version, message type, and word-sized header length, then parse existing response bodies at the decoded offset. Keep the change in the patch stack and compile the real helper in the existing C fixture.

**Tech Stack:** C11 helper fixture, Python unittest patch replay, XNU 26.5 GitHub Actions, Poco F1 Fastboot RAM boot.

**Spec:** `docs/superpowers/specs/2026-11-04-adsp-apr-v1-response-header-design.md`

## Global Constraints

- Accept APR versions 0 and 1 only, response message type 1 only, and header lengths from 20 bytes through the received packet size.
- Require the APR packet-size field to equal received bytes and the firmware body size to equal `header_bytes + 20 + service_count*12`.
- Keep one GET_FWK_VERSION request and do not change GLINK ownership or consume/ACK ordering.
- Device checks use `fastboot boot` only, with no `ufsrw=1` or `rootrw=1`; do not write partitions or GPT.

## Review Focus

- A short, undersized, oversized, or unsupported APR header must fail before any body field read.
- Optional header bytes must be skipped using the decoded length, not treated as firmware-version words.
- A mismatched service count must remain rejected even with an otherwise valid v1 header.
- Existing v0 firmware-version and basic-result packets must retain their behavior.

---

### Task 1: Capture v1 response regression in the compiled helper

**Files:**
- Modify: `tools/tests/test_adsp_apr_mode4.c`
- Test: `tools/tests/test_adsp_pas_patch.py`

**Interfaces:**
- Consumes: current `beryllium_adsp_apr_parse_response()` from patches through 1161.
- Produces: a compiled regression fixture that mirrors the captured prefix and expects a valid v1 response.

- [x] Add the 64-byte captured prefix and a 148-byte synthetic packet fixture; assert parse kind, version 2.8.0.0, and service count 8.
- [x] Run the focused regression against patch 1161 and confirm it fails at the APR v1 parse-kind assertion.

### Task 2: Parse bounded APR v1 headers in patch 1162

**Files:**
- Create: `xnu/patches-12377/1162-beryllium-adsp-apr-v1-response-header.patch`
- Modify: `tools/tests/test_adsp_apr_mode4.c`
- Modify: `tools/tests/test_adsp_pas_patch.py`

**Interfaces:**
- Consumes: the APR response parser and the failing fixture from Task 1.
- Produces: `beryllium_adsp_apr_parse_response()` that validates version/message type/header bounds and parses payloads from `packet + header_bytes`.

- [x] Update the patch replay and C fixture setup to apply 1162 after 1161.
- [x] Implement version 0/1, response type 1, header-size bounds, exact packet size, dynamic basic-result/body offsets, and exact service-count length.
- [x] Add rejection cases for unsupported version, wrong message type, short/oversized header, packet-size mismatch, and inconsistent service count; retain reserved-field checks.
- [x] Run the focused suite (46/46) and full `python3 -I -m unittest discover -s tools/tests -v` (49/49); replay all 13 relevant APR patches through 1162 with `--whitespace=error`.

### Task 3: Review, CI, image, and RAM-boot validation

- [x] Complete an independent read-only code review; no Critical, Important, or Minor findings.
- [x] Update `NOTES.md`, commit without co-author trailers, push, and dispatch XNU 26.5 CI; record run `41804062688` pinned to `c0cfbeab98972950fe07723299100845620b1833`.
- [x] Verify the artifact checksum, empty `errors.txt`, patch application, `XNU Build Done`, and parser strings.
- [x] Package a read-only image, unpack it, and byte-compare the loader and payload; verify required ADSP markers and no root-write boot args.
- [ ] When Fastboot enumerates as unlocked `beryllium`, run `fastboot boot` only and isolate the UDP capture. Record APR result, mount evidence, and UI evidence separately.
- [ ] Update `NOTES.md` with exact test, CI, image, capture, and remaining blocker details.
