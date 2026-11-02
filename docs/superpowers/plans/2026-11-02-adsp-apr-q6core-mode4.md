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
- [ ] Commit and push patch 1151, tests, plan, design, and `NOTES.md`; dispatch the XNU 26.5 KDK build with `gh` and verify patch application, artifact checksums, and empty `errors.txt`.
- [ ] Once exactly one unlocked `beryllium` Fastboot device enumerates, boot a new temporary image with `fastboot boot` and a fresh UDP capture. Record peer OPEN wire fields and the exact boot continuation.
- [ ] Use the observed wire frame to select the next APR protocol change; do not infer channel success or audio from GLINK base negotiation alone.

## Completion report

Summarize the new mode, helper/test/build results, exact Fastboot/APR observation, and the remaining gap between Q6Core reachability and audio playback.
