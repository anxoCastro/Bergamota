# ADSP APR rejected-response diagnostics plan

> **For agentic workers:** execute the steps in order and record the exact build and boot evidence in `NOTES.md`.

**Goal:** Capture enough of the rejected Q6Core `GET_FWK_VERSION` APR payload to identify which header, opcode, or length check fails.

**Design:** `docs/superpowers/specs/2026-11-04-adsp-apr-response-payload-diagnostics-design.md`

**Scope:** Add a bounded 64-byte hex prefix to the malformed-response path. Do not change acceptance behavior.

## Tasks

- [x] Add compiled prefix-boundary tests and a source contract for the malformed `TX_DATA` diagnostic; confirm both fail against patch 1160.
- [x] Add the 64-byte bound helper and log payload length plus the bounded hex prefix, preserving existing consume/ACK/reject order.
- [x] Run focused tests (46/46) and full suite (49/49); replay patches 1131–1161; verify strict patch application and source equality.
- [x] Independently review the diff; no Critical, Important, or Minor findings. Commit patch 1161 as `6f62c61a1a5c5a5fd381a8ad895884fa87644e4a`.
- [ ] Push the code and handoff commits; dispatch XNU 26.5 GitHub Actions and record the run ID.
- [ ] Verify the pinned CI result/artifact and package a read-only image with `sweetarm64e=1`, `sweetadsp=4`, and the existing ADSP markers.
- [ ] When Fastboot enumerates, RAM-boot and capture the response bytes; use those bytes to choose the next protocol change. Record mount/UI state without inference from missing lines.
- [ ] Update `NOTES.md` with exact test, commit, CI, image, capture and remaining-blocker evidence.
