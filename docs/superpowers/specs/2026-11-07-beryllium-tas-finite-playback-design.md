# Beryllium finite TAS speaker playback design

## Intent

Add the first audible Poco F1 speaker test to the iOS 26 XNU port. It is a
short, one-shot hardware smoke test that joins the already tested Q6ASM/ADM/AFE
PCM route with the Beryllium TAS2559/TAS2560 amplifier pair. It is for device
bring-up, not a user-facing audio driver.

## Current evidence

- `sweetadsp=7` completed the four-period 48 kHz S16 stereo Q6ASM route, got
  matching WRITE_DONE events, restored AFE and SMMU state, and left reset GPIOs
  12/76 low. This establishes the DSP data path but not audible sound.
- `sweetadsp=8` read both TAS IDs over SE5 GPIO85/86 and verified both reset
  lines low afterward.
- The local postmarketOS firmware and the Beryllium community bundle contain
  byte-identical `tas2559_uCDSP.bin` (SHA-256
  `62c8d6454dac220cd953646623d8ba329c10177a8aca2c02031948ebd0d2e05c`). The
  bundle's left/right `.ftcfg` files use DDC `E10`, PPC3 3.0.6-beta.17, and
  configuration indices 0 and 5, at 48 kHz.
- A read-only mount of this phone's `/dev/block/sda14` found
  `/persist/audio/tas2559_cal.bin`; the Sphinx parser accepts its 475-byte
  structure as one calibration record for program/configuration 0. The paired
  text result reports success and both measured Re values are inside their
  channel-specific `.ftcfg` limits. Firmware, calibration and phone dumps stay
  in ignored local artifacts.
- Sphinx's enable path attempts the persistent calibration load but treats a
  missing file as nonfatal. For this test, however, the exact phone calibration
  is available and should be supplied to the XNU path.

## Goals

- Add opt-in `sweetadsp=9`; leave modes 0–8 behavior unchanged.
- Keep proprietary firmware/calibration out of Git and CI. A local, ignored
  RAM-boot payload supplies the verified firmware and phone calibration as
  bounded `/chosen` ADT properties. The `.ftcfg` files remain offline factory
  references; they are not needed by playback at runtime.
- Parse only the validated firmware format/version and selected program 0,
  configuration 0, plus the calibration record for program/configuration 0.
  Validate every offset, count, block type, command, CRC field and referenced
  payload length before the first amp write. Reject incomplete or mismatched
  input without releasing either reset.
- Reuse the current mode-7 Q6 route and exact four-period, 100 ms, 48 kHz
  stereo PCM buffer. Preserve its 512/32768 sample peak (about -36 dBFS).
- Follow Beryllium's Linux setup order for hardware reset, software reset,
  defaults, program, PLL/configuration, calibration, startup, post-power-up
  block, and unmute. Do not request an elevated VBoost override.
- Mute, shut down, assert and verify both active-low resets, restore GPIO85/86
  pin state, and run the existing ASM/ADM/AFE/SMMU cleanup after the final
  WRITE_DONE. On uncertain completion, keep allocated PCM/page-table memory
  reserved until reboot, consistent with the existing mode-7 fail-closed rule.

## Non-goals and failure conditions

- No continuous playback, sound server, iOS HAL/AudioServerPlugIn, headphone,
  microphone, handset, Bluetooth, or call route.
- No factory calibration procedure, calibration-file generation, speaker
  tuning, volume control, or guessed register/boost values.
- No UFS/partition/GPT/userdata write. Hardware tests use only temporary
  `fastboot boot` images.
- Any malformed/missing ADT input, firmware CRC mismatch, unsupported command,
  bus timeout/NACK, reset readback failure, APR error/timeout, or cleanup
  uncertainty aborts the sequence and leaves/reasserts both amps in reset.
- Audible output cannot be inferred from a successful DSP/amp protocol trace;
  the device still needs human listening confirmation.

## Approaches

1. Embed TAS binaries in the XNU patch or CI kernel. Rejected: the blobs are
   proprietary, cannot be committed, and are unavailable to public CI.
2. Read the files from the installed iOS root at runtime. Rejected for this
   milestone: no stable firmware-file interface exists in the kernel path,
   and it would couple a one-shot bring-up mode to root filesystem access.
3. Add the firmware and calibration bytes to the local boot ADT via
   `mkpayload.py`, then read them with the existing SecureDT API. Selected:
   this keeps CI reproducible for the code, keeps blob distribution local and
   ignored, and makes the exact bytes available before the amplifier is
   touched.

## Proposed sequence

1. Parse and validate both ADT properties fully; validate program 0/config 0,
   the one matching calibration record, and every block CRC before MMIO or
   reset release.
2. Run existing ADSP/PAS, GLINK/APR discovery and mode-7 SMMU/ASM/ADM/AFE
   preparation while TAS reset stays asserted.
3. Snapshot GPIO85/86 mux state, set open-drain SE5 bus state, assert/read back
   resets low, release/read back resets, and verify both I2C devices ACK their
   identity reads.
4. Load configuration while muted. Preserve the firmware's 48 kHz configuration
   and use the driver's default VBoost state; never set a forced boost level.
5. Unmute only after the route is ready. Submit the existing four 25 ms PCM
   periods, wait for each matching WRITE_DONE, mute, shut down, and assert both
   resets low.
6. Restore bus mux state and only then close/unmap ASM/ADM/AFE and restore SMMU
   ownership using the established mode-7 gates.

## Verification

- Host fixtures exercise firmware/calibration parser lengths, truncation,
  malformed commands, block CRC outcomes, profile matching, and ordered amp
  cleanup. Fixtures are synthetic and contain no proprietary bytes.
- Patch replay and `git diff --check` pass; changed XNU translation units build
  in XNU 26.5 CI.
- A local RAM-only image uses the ignored, hash-verified firmware/calibration
  files. Device logs must show each stage, four matching period completions,
  verified reset-low and GPIO restore, and successful APR/SMMU cleanup.
- Verify no partition/GPT write; after the test, Fastboot/ADB are expected to
  disappear during the XNU boot and the phone can be returned to Fastboot.

## Implementation calls

- Keep `sweetadsp=7` as the amp-off DSP baseline and implement mode 9 as a
  separate path that shares its PCM/route helpers.
- Pass only `tas2559_uCDSP.bin` and `tas2559_cal.bin` through ADT. Do not pass
  `.ftcfg` into the kernel.
- Implement the exact command subset used by the selected program/config and
  calibration blocks. If the selected data uses an unsupported operation or
  requires an unverified register behavior, stop and expand the design before
  any hardware write.
- No new forced VBoost command is added. Playback remains blocked if Linux's
  default-state behavior cannot be mapped exactly.
