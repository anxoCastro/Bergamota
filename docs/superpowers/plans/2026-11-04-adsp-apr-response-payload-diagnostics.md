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
- [x] Push the code and handoff commits; dispatch XNU 26.5 GitHub Actions run `41794907876` (job `126456606597`, pinned head `95b85eb1cfc0d67821b19219a5c9ab8a4787c114`).
- [x] Verify run 41794907876, artifact checksums, empty `errors.txt`, patch 1161 log entry, Mach-O kernel string, and package/unpack a read-only image with `sweetarm64e=1`, `sweetadsp=4`, and the existing ADSP markers.
- [x] RAM-boot when Fastboot enumerated; capture the bounded APR response prefix. It confirms a version-1 32-byte header and matching 148-byte length; record mount/UI evidence separately.
- [x] Update `NOTES.md` with exact test, commit, CI, image, capture, mount/UI limits, and the parser root cause.
