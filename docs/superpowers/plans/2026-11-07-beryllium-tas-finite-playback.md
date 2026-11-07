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

- [ ] **Step 1: Write failing tests** for property insertion, round-trip bytes, no-argument compatibility, one-file-only rejection, duplicate/conflicting property rejection, and oversize input rejection.
- [ ] **Step 2: Run** `python3 -I -m unittest discover -s tools/tests -p 'test_beryllium_tas_payload.py' -v`; confirm the new CLI/property tests fail.
- [ ] **Step 3: Implement** the two paired CLI inputs and safe ADT property insertion. Do not put blob paths, bytes, or hashes in source control.
- [ ] **Step 4: Run payload tests** and confirm an existing payload build with neither new argument is byte-identical to its old ADT output.
- [ ] **Step 5: Commit** `feat(audio): pass local TAS data through boot ADT`.

### Task 3: Bounded TAS setup and mode-9 finite playback

**Files:**
- Modify: staged `bsd/dev/beryllium_adsp.c`, generated as `xnu/patches-12377/1190-beryllium-tas-finite-playback.patch`.
- Modify: staged `bsd/dev/beryllium_tas_id_probe.h` only if shared bus helpers need a small explicit extension.
- Test: extend `tools/tests/test_adsp_tas_probe.c` or add `tools/tests/test_beryllium_tas_playback.c` with a scripted I2C/MMIO/route fixture.
- Test: `tools/tests/test_beryllium_tas_playback.py` for ordered patch replay and mode dispatch.

**Interfaces:**
- Add `adsp_tas_finite_playback_start()` behind `sweetadsp=9`.
- Reuse the current mode-7 PCM allocation, SMMU map, ASM/ADM/AFE sequence and four matching WRITE_DONE checks through a shared internal route helper; do not alter mode-7 behavior.
- Consume the two SecureDT properties and parser result from Tasks 1–2.

- [ ] **Step 1: Write failing state-machine tests** for reset-held parse failure; exact reset/identity/program/PLL/config/calibration/startup/post-powerup/unmute/period/mute/shutdown/reset/restore order; no forced VBoost; and cleanup on each injected I2C/APR/SMMU error.
- [ ] **Step 2: Run** `python3 -I -m unittest discover -s tools/tests -p 'test_beryllium_tas_playback.py' -v`; confirm the tests fail before mode 9 exists.
- [ ] **Step 3: Implement** complete ADT parse before GPIO or I2C changes, bounded register/bulk/sleep commands, CRC reset/readback checks, and Beryllium's default VBoost handling. Reject any command not explicitly supported. Open speaker output only after the route is ready; after the fourth event mute and shut down before reset/pin/SMMU cleanup.
- [ ] **Step 4: Run** both TAS probe/playback fixtures and `python3 -I -m unittest discover -s tools/tests -v`; confirm modes 7/8 fixtures remain unchanged.
- [ ] **Step 5: Replay the complete ordered XNU patch series** through 1190 and byte-compare each modified staged source with the source used to generate the patch.
- [ ] **Step 6: Commit** `feat(audio): add finite Beryllium speaker playback mode`.

### Task 4: Build and RAM-only device validation

**Files:**
- Modify: `NOTES.md` and `docs/audio/BERYLLIUM_LINUX_AUDIO_REFERENCE.md` with measured results and exact artifact hashes.
- Local only: ignored image and diagnostic capture under `.superpowers/ci/<run-id>/`.

- [ ] **Step 1: Run** `git diff --check`, verify both downloaded calibration files remain ignored, and dispatch the KDK-enabled XNU 26.5 kernel workflow with `gh` for the commit containing patch 1190.
- [ ] **Step 2: Download and inspect CI**: verify patch 1190 applied, build/errors/link diagnostics, artifact checksums, and arm64 Mach-O output.
- [ ] **Step 3: Package a local mode-9 image** using the exact CI kernel plus local ignored firmware/calibration; verify ADT lengths and byte-for-byte payload round-trip, no `ufsrw=1`/`rootrw=1` and no partition-write command.
- [ ] **Step 4: Return the phone to Fastboot, verify `product=beryllium` and `unlocked=yes`, start UDP capture, and issue only `fastboot boot` with the temporary image.
- [ ] **Step 5: Inspect the full log** for parser acceptance, reset state, CRC matches, amp start/mute/shutdown/reset, four WRITE_DONE events, AFE/ADM/ASM close, and SMMU restore. Record any output failure literally; a successful trace is not proof that the speaker was audible.
- [ ] **Step 6: Commit and push** the documentation handoff with exact CI run, image/kernel hashes, observed result, and unfinished work. Keep firmware/calibration and device dumps ignored.
