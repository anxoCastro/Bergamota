# Beryllium ADSP APR/Q6Core Mode 4 Plan

**Goal:** Add an explicit `sweetadsp=4` mode that opens `apr_audio_svc`, publishes the SDM845 `<512 20>` receive intents, sends one read-only Q6Core `GET_FWK_VERSION`, and validates its bounded response.

**Architecture:** Extend the ordered XNU patch series with 1150. Keep `sweetadsp=0`–`3` behavior stable. Extend the cold GLINK ring helpers to send/read only the control and data frames required for one channel. Add a freestanding helper for APR packet encoding/validation and an isolated compiled C harness. All APR work is behind successful mode-3 negotiation.

**Spec:** [2026-11-02-adsp-apr-q6core-mode4-design.md](../specs/2026-11-02-adsp-apr-q6core-mode4-design.md)

## Constraints

- Use the existing `audio-poco-f1-adsp-bootstrap` worktree.
- Preserve modes 0–3 and the existing modem transport.
- Use only `apr_audio_svc`, local GLINK channel ID 1, remote intents and AP-owned 512-byte receive buffers.
- Send one 20-byte APR sequential command for Q6Core service 3, opcode `0x001292c`.
- Never request RX intents, send other APR commands, touch AFE/ASM/PCM/codec, or close GLINK/PAS/QMP/XO.
- Validate exact frame and APR lengths, IDs, domains, services, and counts; bound waits to three seconds per channel-open/response stage.
- Package a read-only root and use `fastboot boot` only. Do not flash partitions or change GPT.
- No co-author trailer; keep device logs and firmware out of Git.

## Task 1: Write executable protocol contract tests first (TDD)

**Files:**
- Create `tools/tests/test_adsp_apr_mode4.c`
- Extend `tools/tests/test_adsp_pas_patch.py`
- Add a pure helper header through patch 1150

- [x] Cover APR GET_FWK_VERSION byte layout, valid response service tables, basic response status, invalid header/domain/service/opcode, truncated/oversized service arrays, GLINK OPEN name/length, INTENT list bounds, and single-fragment TX_DATA bounds.
- [x] Add patch-contract checks for mode selection, call order, channel/service/opcode exclusivity, local intent ownership, no shutdown, and no audio service operations.
- [x] Run focused tests and record the expected red state before driver implementation.

## Task 2: Implement the opt-in mode-4 APR probe

**Files:**
- Create `xnu/patches-12377/1150-beryllium-adsp-apr-q6core-mode4.patch`

- [x] Apply patches 1131–1149 in order to a disposable source tree and derive 1150 from the exact post-1149 source.
- [x] Make mode 3 report successful base negotiation without changing behavior; mode 4 alone enters the new APR stage.
- [x] Send an `OPEN` for `apr_audio_svc`; poll for exact peer OPEN and ACK, acknowledge the peer's ID, reject unknown/malformed commands without consuming them.
- [x] Advertise 20 AP-local 512-byte intents (IDs 1–20); accept only bounded remote intent lists for the matching remote channel.
- [x] Send one APR `GET_FWK_VERSION` through one offered remote intent. Parse only one unfragmented response into a matching AP intent; validate the APR header and exact payload shape; send RX_DONE for that intent.
- [x] Log returned firmware fields/service count or a bounded failure, then stop before any audio operation and leave channel/QMP/XO active.

## Task 3: Test and review

- [x] Compile helper harness with `-std=c11 -Wall -Wextra -Werror` and run all new cases.
- [x] Run focused ADSP tests and repository test discovery; require all existing tests to stay green.
- [x] Apply the full patch series through 1150 and compare derived driver changes to the patch.
- [x] Run `git diff --check`; inspect all SMEM writes and APR opcodes for ownership/scope.

## Task 4: Build and temporary device test

- [x] Dispatch GitHub Actions; verify XNU 26.5, patch 1150 application, artifact checksum, and empty `errors.txt`.
- [x] Package an image using the verified Beryllium Enxerto loader and ADT, with `sweetadsp=4`, both required ADSP markers, and no `ufsrw=1`/`rootrw=1`.
- [x] Unpack and verify kernel, payload, both ADT markers, boot header, and SHA-256 values.
- [x] Recheck one Fastboot target, `product=beryllium`, and unlocked state. It passed immediately before the test boot.
- [x] RAM-boot only; record send/boot output, mode-4 APR progress, log hash, and boot continuation. The image was loaded with `fastboot boot`; no partition was written.

## Task 5: Handoff and commit

- [x] Update `NOTES.md` after implementation/build/device milestones, recording exact observed results and unresolved work.
- [x] Commit and push the completed source/docs/handoff without co-author trailer.

## Follow-up: Diagnose the rejected peer OPEN

**Observation:** the mode-4 boot completed PAS and GLINK base negotiation, sent `OPEN` for `apr_audio_svc`, and rejected the incoming peer `OPEN` before sending Q6Core. The first capture has no peer ID, `param2`, frame length, or channel-name bytes. Its later read-only root mount failed and `mount[3]` exited status 1; the capture contains no explicit kernel panic.

- [x] Preserve the rejection behavior and add a bounded log of command, remote ID, `param2`, frame length, and at most 32 wire-data bytes in patch 1151.
- [x] Extend ordered-patch contract coverage through 1151; focused test passes and full test discovery passes 39/39.
- [x] Commit and push patch 1151, tests, plan, design, and `NOTES.md`; dispatch the XNU 26.5 KDK build with `gh` and verify patch application, artifact checksums, and empty `errors.txt` (commit `4cfcb8f`; CI run `41536582931`, success).
- [x] Boot a new temporary image with `fastboot boot` after verifying exactly one unlocked `beryllium` target; capture the peer OPEN (log `.superpowers/ci/41536582931/live-udp-apr-open-diagnostics.log`, SHA-256 `4aaaaf8b6e7acd2a0ef1a6df4c86c1e75765240fe449f18c596232282f4e6be3`). This was RAM-only; no partition was flashed.
- [x] Identify the observed OPEN as `fastrpcglink-apps-dsp`, a sibling of `apr_audio_svc` on the Linux SDM845 ADSP edge.

## Follow-up: Ignore one unsupported FastRPC sibling OPEN

**Observation:** patch 1151 captured command 2, remote ID 1, `param2=22`, a 32-byte frame, and the NUL-padded name `fastrpcglink-apps-dsp`. XNU has no FastRPC endpoint, while Linux DTS declares this channel beside `apr_audio_svc` on the same edge.

- [x] Add a compiled helper test for exact FastRPC name, channel ID, length, full aligned frame, and truncated frame.
- [x] Add a driver contract test proving one known FastRPC OPEN is consumed without ACK before the APR wait continues.
- [x] Implement patch 1152: recognize the exact sibling name, consume at most one such frame by advancing AP-owned RX tail, and send no ACK. Duplicate, malformed, and unknown OPEN frames still reject.
- [x] Run focused and full test discovery; verify the ordered patch series through 1152 and inspect ownership/call order. Focused tests passed 2/2; full `python3 -I -m unittest discover -s tools/tests -v` passed 40/40.
- [ ] Commit and push patch 1152, tests, design, plan, and `NOTES.md`; run the XNU 26.5 KDK build with `gh` and verify the artifact.
- [ ] Package a read-only mode-4 image, recheck Fastboot product/unlocked state, and RAM-boot with a fresh UDP capture. Record whether APR `OPEN_ACK`, remote intents, Q6Core request/response, and boot continuation are observed. Do not infer audio from channel discovery.

## Task 6: Consume one unsupported FastRPC sibling OPEN

**Files:**
- `xnu/patches-12377/1152-beryllium-adsp-fastrpc-peer-open.patch`
- `tools/tests/test_adsp_apr_mode4.c`
- `tools/tests/test_adsp_pas_patch.py`
- This plan, the mode-4 design, and `NOTES.md`

**Interfaces:** patch 1151 logs a bounded wire frame; its device capture identifies the peer OPEN name and shape. The new driver path must consume only that exact frame and preserve APR handshake state.

- [x] Add failing tests for the exact FastRPC name, nonzero/matching remote ID, length, aligned frame, truncation, no ACK, and AP-owned RX consumption.
- [x] Add a helper that recognizes only `fastrpcglink-apps-dsp` and a driver branch that ignores at most one such OPEN without ACK.
- [x] Keep APR OPEN validation and ACK behavior unchanged; reject duplicates, malformed frames, and other channel names.
- [x] Run focused tests, ordered patch application through 1152, and full repository test discovery.
- [x] Commit and push the implementation and handoff (`b611f37`).

**Expected:** tests fail against patch 1151, pass with patch 1152; `python3 -I -m unittest discover -s tools/tests -v` passes.

## Task 7: Build and RAM-boot the FastRPC OPEN change

- [x] Dispatch and verify the XNU 26.5 KDK build with `gh`; confirm patch application, checksum manifest and empty `errors.txt` (run `41543909522`, success).
- [x] Package a read-only `sweetadsp=4` image, then verify kernel/payload bytes, ADT regions and the absence of root-write flags.
- [x] Immediately before boot, confirm one unlocked Fastboot target with product `beryllium`.
- [x] Start a fresh UDP/5140 capture and use only `fastboot boot`; record APR channel progress and boot continuation. The boot was RAM-only; the capture shows FastRPC and IPCRTR peer OPENs, then mode 4 stops before Q6Core. No partition was flashed. The later boot capture reaches `exec mount` but contains no explicit panic or SpringBoard confirmation.

**Observed instead of the original expected next event:** after consuming FastRPC without ACK, the device sends an exact `IPCRTR` OPEN. The current driver rejects it, so no APR/Q6Core request is sent. See the new follow-up below.

## Follow-up: Consume one unhandled ADSP QRTR transport OPEN

**Observation:** patch 1152's RAM-boot capture shows command 2, remote ID 2, `param2=7`, a 16-byte frame, and wire data `IPCRTR\0\0`. Qualcomm's SDM845 vendor DTS identifies IPCRTR as the `lpass` IPC-router GLINK transport. The XNU ADSP path does not implement QRTR.

- [x] Add failing helper and driver-contract checks for the exact `IPCRTR` name/frame and one-time consume-without-ACK behavior.
- [x] Implement patch 1153 to consume at most one valid `IPCRTR` OPEN before APR is open, without ACK or QRTR packet handling; preserve rejection of duplicates and all other names.
- [x] Run the compiled helper, focused ADSP tests, full suite, ordered patch application through 1153, and `git diff --check` (41/41; patch applies with `--whitespace=error`).
- [x] Commit/push the patch and handoff (`acafef7`); dispatch XNU 26.5 KDK build run `41651340363`.
- [x] Verify the build artifact: run succeeded on head `acafef7843486c849930160acb34fe88b5d569f3`, artifact `13243160225`, SHA256SUMS verified, and `errors.txt` empty.
- [x] Package the read-only image and RAM-boot after checking `beryllium`/unlocked state. Capture hash and exact boot result are in `NOTES.md`; no partition was flashed.
- [x] Record the observed next OPEN. Capture saw FastRPC then `glink_ssr`, so mode 4 still stopped before Q6Core.

## Task 9: Consume one unsupported GLINK SSR sibling OPEN

**Observation:** 1153's RAM-boot capture reports command 2, remote ID 2, `param2=10`, a 24-byte frame, and bytes `glink_ssr\0` followed by six pad bytes. Qualcomm's G-Link SSR documentation describes restart notifications and channel cleanup carried over G-Link. This XNU probe has no SSR service implementation, so its bounded read-only APR experiment recognizes the exact OPEN and leaves it unacknowledged. If the ADSP requires SSR setup before APR, Q6Core will remain unreachable; a device boot will test this without sending cleanup or restart messages.

- [x] Add failing helper and driver-contract checks for exact `glink_ssr` name/frame and one-time consume-without-ACK behavior; record the red result before implementation.
- [x] Implement patch 1154 with a nonzero matching remote ID, name length 10 including NUL, and exact 24-byte frame; preserve rejection of duplicates and other names.
- [x] Run focused ADSP tests (39/39), full suite (42/42), and ordered patch-application/whitespace checks.
- [x] Commit/push patch 1154, tests, plan, design, and handoff without a co-author trailer (`a70e1af`).
- [x] Dispatch XNU 26.5 KDK build run `41664276962` on head `9bff197e087caeb1927e12d68846b552cf79ec60`; first attempt failed after 1h0m13s with the GitHub annotation “hosted runner lost communication with the server,” without a Build log or artifact. `gh run rerun 41664276962 --failed` started retry job `126052595900` on the same head.
- [ ] Verify patch application, artifact checksums, and empty `errors.txt`.
- [ ] Package a read-only mode-4 image; recheck one unlocked `beryllium` Fastboot target immediately before `fastboot boot`; start a fresh UDP capture. Record APR/Q6Core and boot results, and do not infer audio playback from APR reachability.

## Completion report

Summarize the new mode, helper/test/build results, exact Fastboot/APR observation, and the remaining gap between Q6Core reachability and audio playback.
