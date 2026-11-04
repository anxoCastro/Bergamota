# Plan: Linux-faithful QUAT AFE lifecycle

Spec: `docs/superpowers/specs/2026-11-04-adsp-linux-quat-lifecycle-design.md`

## Global constraints

- Work only on `audio-poco-f1-adsp-bootstrap` in its existing worktree.
- Preserve modes 1–4; mode 4 remains read-only. New AFE control commands are
  gated behind mode 5 and the unique observed AFE API 2.0 record.
- Use Linux tag `sdm845-6.18.2-r0` as the packet/topology reference. Do not
  copy GPL Linux driver code into XNU.
- Port the QUAT AFE control path only: clock set, padded I2S SET_PARAM_V2,
  start, and stop. No PCM/ASM/ADM/codec/GPIO/TAS operations.
- Device validation uses `fastboot boot` only and a read-only inner command
  line. Never flash a partition or run filesystem repair/rootrw.
- Keep Fastboot serials, firmware and raw device captures out of Git. Record
  hashes and paths of ignored captures in `NOTES.md` without serials.
- Commit each completed milestone, with no co-author trailers. Use GitHub
  Actions for the full XNU build.

## Interfaces

- Helper API is added to `bsd/dev/beryllium_adsp_apr_probe.h` through the
  numbered XNU patch series. `test_adsp_linux_afe.c` consumes its packet
  encoders and basic-result parser.
- The driver integration in `bsd/dev/beryllium_adsp.c` consumes those helpers
  and the existing `adsp_apr_poll_response`/remote-intent lifecycle.
- Python static contracts in `tools/tests/test_adsp_pas_patch.py` apply the
  ordered patch chain through the new patch and inspect mode-4/mode-5 scope.
- `docs/audio/BERYLLIUM_LINUX_AUDIO_REFERENCE.md`, the spec, the plan and the
  latest NOTES handoff document the Linux evidence and test assumptions.

## Tasks

### Task 1 — Record Linux implementation and current 1164 evidence

Files: `docs/audio/BERYLLIUM_LINUX_AUDIO_REFERENCE.md`,
`docs/superpowers/specs/2026-11-04-adsp-linux-quat-lifecycle-design.md`,
`docs/superpowers/plans/2026-11-04-adsp-linux-quat-lifecycle.md`, `NOTES.md`.

Steps:

1. Document the Beryllium routes and exact Linux clock/config/start/stop
   packets, including the 36-byte union and the GET_PARAM rejection boundary.
2. Update the top handoff with completed 1164 CI/boot results, the Linux
   findings, and the next implementation action.
3. Run `git diff --check`.

Expected: docs separate observed facts from test assumptions; the 1164 run is
recorded as success; the 80-byte GET status-2 response is not misrepresented
as proof of unsupported speaker audio.

Commit: `docs(audio): document Linux QUAT AFE startup`.

### Task 2 — Add exact packet helper tests (RED first)

Files: `tools/tests/test_adsp_linux_afe.c`,
`tools/tests/test_adsp_pas_patch.py`.

Steps:

1. Add a focused helper-test registration that requires the next numbered
   patch and compiles the new C fixture against the generated helper header.
2. Add fixture assertions for clock SET_PARAM, 36-byte I2S SET_PARAM_V2,
   DEVICE_START/STOP, correlated APR basic-result status, mismatched opcode,
   token, endpoint, header and packet lengths, and the 80-byte status-2 GET
   fixture.
3. Run `python3 -I -m unittest discover -s tools/tests -p 'test_adsp_pas_patch.py' -v`;
   verify it fails because the helper patch/API is missing.

Expected: focused suite fails at the new intended contract, not due to Python
syntax or fixture compilation errors.

### Task 3 — Implement packet helpers in the next XNU patch

Files: `xnu/patches-12377/<next>-beryllium-adsp-linux-afe-lifecycle.patch`,
`tools/tests/test_adsp_pas_patch.py`, `tools/tests/test_adsp_linux_afe.c`.

Steps:

1. Replay the ordered patches through 1164 onto the exact successful CI
   `xnu-buildtree` artifact, then stage the new helper header/function changes
   into the next numbered unified patch.
2. Add exact encoders and a strict generic AFE APR basic-result parser.
3. Change GET status parsing to accept a bounded nonzero status response with
   trailing output bytes while refusing to decode it as I2S configuration.
4. Run the focused Python suite and compiled C fixture.

Expected: Task 2's focused test passes; request packet lengths and fields
match the documented Linux ABI exactly.

Commit: `feat(audio): add Linux-compatible AFE packet helpers`.

### Task 4 — Integrate opt-in mode 5

Files: next numbered XNU patch, focused C/Python tests.

Steps:

1. Extend mode validation to 5 and route only `sweetadsp=5` to the new AFE
   lifecycle; preserve modes 1–4 and the read-only behavior of mode 4.
2. Reuse the existing Q6Core API discovery, remote-intent reuse and APR
   response ownership. Send clock, port config, start, then stop, waiting for
   a successful correlated basic-result before each next command.
3. Stop on any negative/malformed/unmatched response and never add a PCM,
   ASM, ADM, codec or GPIO path.
4. Add static contracts for exact mode gating and outbound command sequence.
5. Run the focused suite and full repository suite.

Expected: mode-4 tests still enforce GET-only behavior; mode-5 tests enforce
the four-command Linux control sequence and stop-on-error behavior.

Commit: `feat(audio): add opt-in QUAT AFE lifecycle probe`.

### Task 5 — Build, boot, capture, and hand off

Files: `NOTES.md`, `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`.

Steps:

1. Replay the numbered patch series with `git apply --whitespace=error` on
   the pinned XNU build-tree baseline; run the full tool test suite and
   `git diff --check`.
2. Cross-compile the modified XNU translation unit locally, then dispatch the
   pinned XNU 26.5 GitHub Actions build and inspect logs/artifact hashes.
3. Package a temporary image with `sweetadsp=5`, read-only userdata args,
   verified loader/ADT and the CI kernel. Inspect the command line and image
   contents before boot.
4. Confirm Fastboot product/unlocked state without recording its identifier;
   use `fastboot boot` only. Capture UDP logs, verify command/response order,
   and leave the phone in Fastboot.
5. Record commit, CI run/job IDs, kernel/image/capture hashes, exact command
   statuses, boot outcome and remaining audio work in NOTES. Commit and push
   the completed milestone.

Expected: full XNU build succeeds; only RAM boot is used; the log identifies
which AFE lifecycle stages succeeded or the first correlated DSP rejection.
No SpringBoard or audible-audio claim is required for this control-plane test.

## Review focus

- 36-byte Linux union is sent with all 16 pad bytes zero; no 20-byte SET.
- Clock is a service SET_PARAM (`0x100f3`), not another I2S GET or port
  SET_PARAM_V2; clock fields and token are exact.
- Every command is gated by the prior successful basic result; response
  endpoint, token, original opcode and status are checked before continuing.
- Mode 4 remains query-only and mode 5 is the only new writer.
- No extra PAS/GLINK teardown, speaker calibration, PCM or codec behavior is
  introduced.
