# Beryllium TAS2559/TAS2560 Identity Probe

## Intent

Move speaker bring-up forward by confirming that XNU can reach the Poco F1's
paired TAS2559/TAS2560 amplifiers over the real board I2C route. Linux places
the pair on SDM845 GENI SE5, with TAS2559 at 7-bit address `0x4c`, TAS2560 at
`0x4d`, reset lines GPIO12/GPIO76, and SE5 I2C pins GPIO85/GPIO86. The current
XNU mode-7 test keeps both reset lines low and never touches I2C.

The first XNU milestone is a finite, opt-in identity probe. It will establish
the bus and device identities without programming the audio signal path or
activating either speaker power stage.

## Goals

- Add an independent `sweetadsp=8` diagnostic that leaves mode 7 unchanged.
- Use Beryllium TLMM GPIO85/86 for low-speed open-drain I2C when SE5 FIFO is
  unavailable or configured GSI-only.
- Read and log TAS2559 PGID from book 0/page 0/register `0x03` and TAS2560 ID
  from book 0/page 0/register `0x7d`.
- Map and verify both active-low reset lines low before setup, release them
  only after bus prerequisites are validated, and reassert low on every exit.
  If TLMM mapping/readback is unavailable, abort without releasing either line.
- Preserve the original SDA/SCL pin configuration and leave both lines
  released; abort if either line is stuck low or transactions fail.
- Log bounded results and return without touching AFE, ASM, ADM, PCM, DMA,
  firmware, calibration, boost, or speaker playback.

## Non-goals

- Uploading the proprietary `tas2559_uCDSP.bin` program or `.ftcfg` files.
- Applying `/persist/audio/tas2559_cal.bin`, setting VBoost, unmuting, or
  producing sound.
- Implementing the complete Linux GENI/GPI DMA controller or the iOS audio
  userland bridge.
- Changing existing ADSP modes or SMMU ownership logic.

## Constraints and safety

- All test boots use temporary `fastboot boot` images. Do not flash partitions,
  modify GPT, or write userdata.
- Default behavior is unchanged unless `sweetadsp=8` is explicitly selected.
- The mode-7 test remains unchanged and keeps resets asserted.
- The TAS firmware, calibration, and device dumps stay outside git.
- The SDM845 Linux pinctrl source uses the EAST TLMM region at `0x03500000`
  for GPIO76, GPIO85, and GPIO86; with `REG_SIZE=0x1000`, GPIO85/86 windows
  are `0x03555000` and `0x03556000`. These offsets must be pinned by host tests
  before any MMIO access is attempted.
- I2C is open-drain only: a logical high releases SDA/SCL as inputs; XNU must
  never actively drive either line high. Bound clock-stretch waits to 1 ms and
  use a finite retry/transaction budget.
- Reset is active-low. Probe the hardware identity registers only; do not
  write amplifier data registers. If GPIO mapping is unavailable, abort before
  reset release; after successful mapping, any later failure goes through a
  reset-low cleanup and is reported if the readback does not confirm it.

## Approaches considered

1. **Port the downstream Sphinx driver immediately.** It provides the complete
   program/configuration and Beryllium VBoost sequence, but the matching blobs
   are not present in the workspace and this bypasses the need to prove XNU's
   bus and board wiring first.
2. **Use the mainline TAS2559 codec driver as the playback implementation.**
   It is smaller and already used by postmarketOS, but Beryllium's speaker is
   documented as quiet because mainline lacks the downstream VBoost logic.
3. **Add a bounded identity-only probe first (selected).** It isolates TLMM,
   reset, and I2C bring-up from ADSP/PCM and high-power amplifier setup. The
   existing mode-7 ADSP path already supplies the later prerequisite for a
   controlled speaker experiment.

## Design decisions

- Reuse the project's existing GPIO open-drain bit-bang pattern, but keep the
  TAS probe bounded and separate from touch state and touch pins.
- Use a new boot argument value rather than extending mode 7, so the known
  ADSP sequence and its safe reset-low behavior remain unchanged.
- Snapshot and restore SDA/SCL TLMM configuration; leave reset pins low after
  any mapped probe, including all I2C error paths. If the initial TLMM map is
  unavailable, no reset state is claimed.
- Treat ACKed reads of both identity registers as the only pass condition.
  The downstream driver logs these bytes but does not validate fixed expected
  values, so XNU reports raw values rather than inventing an allowlist. NACK,
  stuck lines, or timeout are failed probe results.

## Open implementation work

- Confirm the existing ADSP source's boot-argument dispatch and integrate mode
  8 without changing modes 0-7.
- Add a fake-GPIO protocol harness for I2C framing/ACK/NACK/timeout plus static
  patch checks for physical GPIO mapping, reset sequencing, and mode isolation.
- Build in GitHub Actions and only RAM-boot once one unlocked Beryllium device
  is present in Fastboot.
- After identity is confirmed, locate the matching proprietary firmware and
  establish which vendor configuration and calibration are safe before
  designing any power-up sequence.
