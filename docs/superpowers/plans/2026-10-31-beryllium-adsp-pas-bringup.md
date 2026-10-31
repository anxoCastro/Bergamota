# Beryllium ADSP PAS bring-up plan

## Goal

Add an operator-selected XNU stage that attempts to start the Poco F1 ADSP with PAS ID 1, then verify the result with a temporary Fastboot boot. Keep normal boots and `sweetadsp=1` read-only. Stop before GLINK, APR, or audio.

The user authorized temporary phone experiments on 2026-10-31. `sweetadsp=2` is the explicit request to load/authenticate/reset ADSP. It may intentionally reset ADSP, but must refuse if the read-only SMP2P snapshot already reports ready or fatal. If SMP2P is unavailable, report state as unknown and proceed only because the operator selected mode 2.

## Constraints

- Never call PAS unless `sweetadsp=2` is present, the board is Beryllium, firmware and memory checks pass, PAS ID 1 is supported, and QMP/XO prerequisites succeed.
- Parse the entire split MDT; verify every needed sidecar is readable and exactly `p_filesz`; assemble ELF-header-plus-hash metadata as Qualcomm MDT expects.
- Verify the live ADT's ADSP carveout and its disjointness from XNU-managed RAM before mapping or writing `0x8c500000..0x8e300000`.
- Use the existing SMC64 SIP convention with bounded interrupted/busy handling. Never reuse GPU PAS ID 13.
- After successful AUTH_AND_RESET, wait at most five seconds for Q6V5 ready; only shut down an ADSP instance this invocation started, and only on its ready timeout.
- Do not write SMP2P item 429/443 or GLINK/APR. Do not modify GPT or flash any partition. Boot test payloads only with `fastboot boot`, without `ufsrw=1` or `rootrw=1`.
- Keep handset firmware and captures in ignored `.superpowers/` paths. Commit source, tests, plan, design, and handoff only.

## Review Focus

- Default boot and `sweetadsp=1` never call SCM, change QMP/RPMh, or map/write the ADSP carveout.
- `sweetadsp=2` repeats the read-only state check immediately before PAS and aborts on ready/fatal; missing/malformed state is logged as unknown and proceeds only because mode 2 is explicit.
- Every needed split MDT sidecar and hash metadata byte is validated before the first PAS call. Segment offsets, bounds, and BSS zeroing match Linux's MDT loader.
- The ADT marker must equal the captured board carveout and be disjoint from XNU RAM before mapping/writing.
- Interrupted calls preserve original x1-x5 and resume token x6; only successful AUTH_AND_RESET from this invocation can be shut down after ready timeout.
- No outbound SMP2P writes, GLINK/APR, or audio work.
- Phone experiment uses only `fastboot boot`, with no filesystem or partition writes.

### Task 1: Add the opt-in carveout marker and regression tests

**Files:**
- Modify: `tools/mkpayload.py`
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Modify: `docs/superpowers/specs/2026-10-31-beryllium-adsp-pas-startup-design.md`

**Interfaces:**
- Payload builder accepts only verified range `0x8c500000:0x01e00000` and adds `/chosen/sweet-adsp-region` only when requested.
- Task 2 consumes this marker and the PAS patch safety contracts.

- [x] Record the corrected TWRP interpretation: `OFFLINING` is Linux's initial software tracker value, not physical ADSP status.
- [x] Record Linux PAS/MDT order and resource lifetimes from primary sources; keep XO and QMP load-state asserted after ready because Linux clears them only during unprepare/stop.
- [x] Write tests for optional/exact ADT marker serialization and PAS patch safety; make an absent patch fail with assertions rather than a missing-file error.
- [x] Run focused tests and observe expected failures before implementation.
- [x] Implement the marker CLI/helper and rerun focused tests; PAS patch assertions should still fail until Task 2.
- [x] Update the design to permit explicit mode-2 start with unknown state and remove the stale fail-closed-on-unknown requirement.
- [ ] Commit the plan/design/test/builder milestone.

**Check:** default payload build is unchanged; explicit marker contains exactly two little-endian u64 values; other ranges are rejected; remaining red tests name the absent production patch.

### Task 2: Implement and validate the PAS-only stage

**Files:**
- Create: `xnu/patches-12377/1132-beryllium-adsp-pas-startup.patch`
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Modify: `docs/superpowers/specs/2026-10-31-beryllium-adsp-pas-startup-design.md`

**Interfaces:**
- Consumes Task 1's exact ADT marker and the reviewed status/resource contracts.
- Adds narrow QMP and RPMh XO operations to helpers from patches 1106 and 1105.

- [ ] Extend 1131 without changing default or `sweetadsp=1` read-only behavior.
- [ ] Validate complete MDT, hash metadata, every split sidecar, carveout marker, and XNU RAM bounds before PAS calls.
- [ ] Implement SCM availability checks and PAS ID 1 INIT_IMAGE → MEM_SETUP → segment copy/zero → AUTH_AND_RESET.
- [ ] Poll SMP2P read-only for ready/fatal for at most five seconds; SHUTDOWN only after this invocation's successful AUTH_AND_RESET.
- [ ] Preserve QMP load-state and XO after ready; no handover event is implemented in this stage.
- [ ] Run focused tests, full Python suite, patch-series application/order checks, and `git diff --check`.
- [ ] Commit source, tests, and design updates.

**Check:** local checks pass; diff shows no GLINK/APR, outbound item 429/443 writes, UFS write flags, or firmware artifacts.

### Task 3: Build and test on the phone

**Files:**
- Modify: `NOTES.md`

- [ ] Push the feature branch and use `gh` to run the repository's kernel workflow; record run ID, artifact hash, and warnings.
- [ ] Verify payload includes `sweetadsp=2` and `--adsp-region 0x8c500000:0x01e00000`, excludes `ufsrw=1`/`rootrw=1`, with no partition-flash command involved.
- [ ] Start UDP kernel-log capture, verify exactly one Fastboot `beryllium`, and use `fastboot boot` only.
- [ ] Record PAS call results, Q6V5 ready/fatal result, later mount result, and return phone to Fastboot after the observation.
- [ ] Commit the exact build/device handoff.

**Check:** device reaches Q6V5 ready, or logs a named pre-PAS prerequisite failure; XNU continues booting and no partition is flashed.

## Completion criteria

- A default boot and mode 1 remain free of PAS calls and writes.
- Mode 2 either aborts before the first mutating PAS call on a named failed prerequisite, or reaches PAS ID 1 authentication and reports whether Q6V5 ready arrived within five seconds.
- The commit and `NOTES.md` handoff state exact evidence separately from inference. The stage remains independent of GLINK/APR and audio routing.
