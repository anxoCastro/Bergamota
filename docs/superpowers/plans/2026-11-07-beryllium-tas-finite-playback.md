# Beryllium TAS finite playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add opt-in `sweetadsp=9` to play the existing 100 ms low-amplitude Q6 tone through Beryllium's calibrated TAS2559/TAS2560 pair.

**Architecture:** Add an offline firmware/calibration property pair to the local payload ADT. In XNU, validate and parse the bounded TAS inputs before MMIO, then run the matching Linux Beryllium program/config/calibration sequence around the already-tested mode-7 PCM route. Mode 7 and the public CI kernel remain blob-free.

**Tech Stack:** C (XNU kernel and host fixtures), Python 3 (`mkpayload.py`, unittest), GitHub Actions XNU 26.5/Xcode 26.1, Enxerto RAM boot.

**Spec:** `docs/superpowers/specs/2026-11-07-beryllium-tas-finite-playback-design.md`

## Global Constraints

- Keep proprietary firmware, calibration, `.ftcfg`, device dumps, and the phone serial out of Git.
- `sweetadsp=7` remains amp-off and behavior-compatible; `sweetadsp=8` remains identity-only; `sweetadsp=9` is explicit opt-in.
- Use only program 0, configuration 0, the matching calibration record, 48 kHz S16 stereo and four 25 ms PCM periods at the existing sample peak 512/32768.
- Never request forced VBoost; use the verified downstream default-state behavior.
- On uncertain state, leave both resets asserted and retain DMA/SMMU resources until reboot.
- Hardware validation uses a temporary `fastboot boot` image only; no partition, GPT, or userdata writes.

## Review Focus

- Truncated or oversized ADT firmware/calibration properties must fail before GPIO/reset changes; test every header and length boundary.
- Wrong firmware version, profile mapping, CRC, or calibration target must fail before writes; use synthetic accepted/rejected fixtures.
- Unsupported block opcodes and invalid bulk-write spans must fail closed; test command decoding and no-write ordering.
- I2C NACK, clock-stretch timeout, or reset readback failure must mute/reassert reset and restore pin state; test all cleanup branches.
- Missing/mismatched APR completion or SMMU cleanup must not release DMA memory or claim success; extend the existing sequence fixture for mode 9.

---

### Task 1: TAS firmware and calibration parser

**Files:**
- Create: `tools/beryllium_tas_firmware.h`; patch 1190 will carry a byte-identical copy into `bsd/dev/` for the XNU build.
- Test: `tools/tests/test_beryllium_tas_firmware.c` (synthetic fixtures, no proprietary data).
- Test: `tools/tests/test_beryllium_tas_firmware.py` (compile/run the C fixture and check patch replay).

**Interfaces:**
- Produces `beryllium_tas_parse_firmware(data, size, &fw)` and `beryllium_tas_parse_calibration(data, size, program, config, &cal)`; return false on malformed/unsupported input.
- Produces iteration access to validated program 0, configuration 0, calibration 0 and their bounded blocks/commands; no allocations or writes during parse.

- [ ] **Step 1: Write failing synthetic parser tests** for the 0x401 firmware layout, legacy 0x100 calibration layout, exact profile relation, block checksums, truncated header/name/block, count overflow, trailing bytes, and unsupported opcode/span.
- [ ] **Step 2: Run** `python3 -I -m unittest discover -s tools/tests -p 'test_beryllium_tas_firmware.py' -v`; confirm the parser tests fail before the helper exists.
- [ ] **Step 3: Implement** overflow-safe big-endian readers and version-gated record/block parsing in `bsd/dev/beryllium_tas_firmware.h`. Require firmware DDC `E10`, driver version `0x401`, program/configuration mapping 0/0, exactly one calibration record for 0/0, and full input consumption. Validate block checksum metadata before execution; runtime hardware CRC readback remains part of the writer.
- [ ] **Step 4: Run parser tests** and confirm every positive/negative fixture passes.
- [ ] **Step 5: Commit** `feat(audio): parse beryllium TAS playback data`.

### Task 2: Local payload ADT firmware transport

**Files:**
- Modify: `tools/mkpayload.py` (`add_tas_firmware_to_adt`, `build`, CLI arguments).
- Test: `tools/tests/test_beryllium_tas_payload.py`.

**Interfaces:**
- CLI adds `--tas-firmware PATH --tas-calibration PATH`; both are required together and each is capped at 128 KiB.
- ADT properties: `/chosen/sweet-tas-firmware` and `/chosen/sweet-tas-calibration`, exact raw byte payloads.

- [x] **Step 1: Write failing tests** for property insertion, round-trip bytes, no-argument compatibility, one-file-only rejection, duplicate/conflicting property rejection, and oversize input rejection.
- [x] **Step 2: Run** `python3 -I -m unittest discover -s tools/tests -p 'test_beryllium_tas_payload.py' -v`; the missing helper and CLI flags were observed failing before implementation.
- [x] **Step 3: Implement** the two paired CLI inputs and safe ADT property insertion. Do not put blob paths, bytes, or hashes in source control.
- [x] **Step 4: Run payload tests**; no-input insertion is byte-identical to the input ADT, and an ADT round-trip with the exact local proprietary files preserved every firmware/calibration byte. Existing payload build options remain unchanged when neither argument is passed.
- [x] **Step 5: Commit** `feat(audio): pass local TAS data through boot ADT` (`0b78ada`, pushed).

### Task 3: Mode-9 SecureDT parser preflight (patch 1190)

**Files:**
- Modify staged `bsd/dev/beryllium_adsp.c`; add a byte-identical copy of `tools/beryllium_tas_firmware.h` under `bsd/dev/`, generated as `xnu/patches-12377/1190-beryllium-tas-finite-playback.patch`.
- Test: `tools/tests/test_beryllium_tas_playback.py` for parser copy, mode dispatch, no-hardware ordering, and complete ordered patch replay.

**Interfaces:**
- `sweetadsp=9` reads both bounded `/chosen` payloads, validates profile 0/0, and returns without ADSP or TAS hardware access.
- Modes 7 and 8 remain untouched.

- [x] **Step 1: Write failing patch-integration tests** proving patch 1190 is absent and the validated helper isn't copied into XNU.
- [x] **Step 2: Run** `python3 -I -m unittest discover -s tools/tests -p 'test_beryllium_tas_playback.py' -v`; the missing patch failed as expected.
- [x] **Step 3: Implement** parser-only SecureDT preflight; no reset, GPIO, I2C, PAS, or APR access occurs in mode 9 yet.
- [x] **Step 4: Run** parser/preflight tests and replay the complete ordered patch series through 1190, byte-comparing driver and parser helper with staged sources.
- [ ] **Step 5: Build XNU 26.5 and commit** the parser-preflight patch.

### Task 4: TAS programming and finite playback

**Files:**
- Modify staged `bsd/dev/beryllium_adsp.c` and TAS helper(s), generating the next ordered XNU patch.
- Test: scripted I2C/MMIO/route state machine in `tools/tests/test_beryllium_tas_playback.c` and patch replay in `tools/tests/test_beryllium_tas_playback.py`.

**Interfaces:**
- Reuse the mode-7 PCM allocation, SMMU map, ASM/ADM/AFE setup and four matched WRITE_DONE checks through explicit pre-play/post-play hooks; mode 7 behavior must remain unchanged.
- Mode 9 consumes the two validated SecureDT views. Failures keep resets low and retain uncertain DMA/SMMU resources until reboot.

- [ ] **Step 1: Write failing state-machine tests** for reset-held parse failure; exact reset/identity/program/PLL/config/calibration/startup/post-powerup/unmute/period/mute/shutdown/reset/restore order; no forced VBoost; and cleanup on each injected I2C/APR/SMMU error.
- [ ] **Step 2: Implement** exact Linux Beryllium defaults and supported write/bulk/sleep commands, CRC reset/readback checks, and pre-play/post-play hooks. Reject all unsupported operations. Keep the speaker muted until the DSP route is ready; mute and shut down after the final event and before GPIO/SMMU cleanup.
- [ ] **Step 3: Run** the TAS probe/playback fixtures and `python3 -I -m unittest discover -s tools/tests -v`; confirm modes 7/8 remain unchanged.
- [ ] **Step 4: Replay the complete ordered XNU patch series** through the playback patch and byte-compare each modified staged source.
- [ ] **Step 5: Commit** the finite Beryllium speaker playback mode.

### Task 5: Build and RAM-only device validation

**Files:**
- Modify: `NOTES.md` and `docs/audio/BERYLLIUM_LINUX_AUDIO_REFERENCE.md` with measured results and exact artifact hashes.
- Local only: ignored image and diagnostic capture under `.superpowers/ci/<run-id>/`.

- [ ] **Step 1: Run** `git diff --check`, verify local calibration inputs remain ignored, and dispatch the KDK-enabled XNU 26.5 kernel workflow with `gh` for the playback patch commit.
- [ ] **Step 2: Download and inspect CI**: verify ordered patch application, build/errors/link diagnostics, artifact checksums, and arm64 Mach-O output.
- [ ] **Step 3: Package a local mode-9 image** using the exact CI kernel plus local ignored firmware/calibration; verify ADT lengths and byte-for-byte payload round-trip, no `ufsrw=1`/`rootrw=1` and no partition-write command.
- [ ] **Step 4: Return the phone to Fastboot, verify `product=beryllium` and `unlocked=yes`, start UDP capture, and issue only `fastboot boot` with the temporary image.
- [ ] **Step 5: Inspect the full log** for parser acceptance, reset state, CRC matches, amp start/mute/shutdown/reset, four WRITE_DONE events, AFE/ADM/ASM close, and SMMU restore. Record any output failure literally; a successful trace is not proof that the speaker was audible.
- [ ] **Step 6: Commit and push** the documentation handoff with exact CI run, image/kernel hashes, observed result, and unfinished work. Keep firmware/calibration and device dumps ignored.
