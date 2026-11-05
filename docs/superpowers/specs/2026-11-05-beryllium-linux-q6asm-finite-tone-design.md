# Beryllium Linux-faithful Q6ASM finite-tone design

## Goal

Build the next Beryllium audio milestone from the working Linux route: send a
bounded PCM stream through Q6ASM, register and route it through ADM to the QUAT
MI2S endpoint, and stop after four 25 ms writes. This checks the DSP DMA and
APR command path. It does not claim audible output or provide an iOS audio
device.

## Evidence and Linux contract

The Beryllium Linux path is MultiMedia1 → Q6ASM → q6routing/ADM → AFE QUAT
MI2S RX (`0x1006`) → TAS2559/TAS2560. The mode-5 XNU probe already received
status 0 for QUAT clock setup, I2S configuration, AFE start and stop. The
mode-6 read-only Apps SMMU probe observed `sCR0=0x210406`, `ID1=0x5000002d`,
76 SMRs, 45 context banks and no valid SMR matching full stream ID `0x1821`.
The same boot showed context bank 3 in use by a non-audio SID. `USFCFG=1`
means unmatched streams fault under the Linux ARM SMMU driver; therefore a
private translation is required before Q6ASM sees the DMA address.
The mode-5 Q6Core service table also advertised ASM service 7/API 1 and ADM
service 8/API 2 exactly once each; mode 7 requires those same unique records
before sending commands.

The Linux ABI values used by this test are:

- MultiMedia1 DAI ID 0; Linux allocates ASM session 1 and then explicitly
  assigns DSP stream ID 1. Stream APR source/destination ports are therefore
  `0x101`; sync-command token is 1. The playback-direction memory-map token
  is `0x100`.
- Four periods of 4,800 bytes make a 19,200-byte PCM buffer. Linux rounds the
  single contiguous region up to 20,480 bytes (five 4 KiB pages) and maps it
  in ASM shared-memory pool 3. The 40-byte `SHARED_MEM_MAP_REGIONS` request
  has one region; the `0x10d93` response's first payload word is the map
  handle.
- `OPEN_WRITE_V3` is 36 bytes total, followed by a 44-byte PCM format update
  for 48 kHz, signed 16-bit stereo, FL/FR channel map `1,2`.
- ADM opens playback path 1 for QUAT (`AFE port 0x1006`, logical port 22),
  NULL COPP topology `0x10312`, 2 channels, 16 bits and 48 kHz. It then maps
  session 1 to the returned COPP ID on the RX matrix. An ADM open response is
  8 bytes and includes the COPP ID.
- ASM sends `RUN_V2`, then four `WRITE_V2` packets, each describing 4,800
  bytes (25 ms). Each write waits for a matching `WRITE_DONE_V2` address
  event before the next request. No unbounded callback loop is used.
- Linux zero-initializes ASM/ADM APR headers: bytes 4–11 (service/domain
  fields) are zero. Their port and token fields are set as above. AFE packet
  headers continue using the already validated AFE service/domain values.

## SMMU ownership

The test owns a private stage-1 context only for full SID `0x1821`. It uses
IOVA `0x10000000` for the 20 KiB buffer, mapped page-by-page to a physically
contiguous kernel allocation. This IOVA is not asserted to be reserved by the
Beryllium device tree; it is safe here only because the selected context is
private and contains no other mappings.

Immediately before installing it, the code re-reads Apps SMMU state. It must
find exactly no matching SMR, a free SMR, and a context bank that is both
unreferenced by every translating S2CR and disabled (`SCTLR.M=0`). Any
existing exact or masked SID match, unavailable SMMU, busy bank, unexpected
register layout or changed ownership aborts the test without modifying the
existing route. The selected SMR/S2CR and context-bank registers are saved;
installation writes only the newly reserved entry and context.

Page tables and PCM memory use permanent kernel allocations. The SMMU helper
maps only the 20 KiB test buffer; it does not reuse the shared DRAM identity
map or USB's context bank 3. It restores the prior SMR/S2CR and context-bank
registers only after the DSP has positively acknowledged stream close,
buffer unmap, ADM close and AFE stop. If an APR response times out or a close
is uncertain, the buffer and translation stay resident and enabled until
reboot, preventing a late DSP DMA from reaching freed or repurposed memory.
The hardware tables use 4 KiB granules; XNU backs their 12 KiB combined
allocation with `round_page()` and verifies physical contiguity at its native
page size before splitting it into three aligned SMMU table pages.

## Amplifier safety and route order

Before audio commands, the test drives Beryllium TAS reset GPIOs 12 and 76
low, verifies the output latches, and leaves both held low until reboot. It
does not write amplifier I2C registers, load TAS firmware, raise either reset
line, or enable a speaker route in userland.

The opt-in mode runs in this order:

1. Drive both TAS resets low and verify them. Then re-probe SID ownership,
   allocate/initialize the PCM buffer, and install its private translation.
2. Open the already validated GLINK/APR apps2 channel and confirm the Q6Core
   API list.
3. Map the buffer in ASM pool 3, open the MultiMedia1 write stream, open the
   ADM QUAT playback COPP, map the matrix route and set PCM format.
4. Send the known-working AFE clock/configuration/start sequence.
5. Send ASM RUN and exactly four WRITE_V2 packets, validating each completion.
6. Close ASM and unmap its shared buffer; close ADM; stop QUAT. Restore SMMU
   state only if all required acknowledgements arrived.

The pattern is a low-amplitude 1 kHz sine wave in four distinct 25 ms periods
inside the 20 KiB allocation. The TAS devices remain in reset, so the acceptance
criteria concern APR/SMMU/ASM/ADM lifecycle and DMA completion only, not
speaker sound.

## Failure behavior

- Abort before any SMMU write if stream ownership is not exactly the recorded
  no-match state or no free SMR/context bank exists.
- Abort before PCM transfer if either reset output cannot be verified low.
- Abort before route setup unless the Q6Core table contains exactly one ASM
  service 7/API 1.0 record and one ADM service 8/API 2.0 record.
- Validate every command response against the expected opcode, token, ports,
  service/domain fields and response length. Reject nonzero DSP status.
- The WRITE_DONE event must return the submitted IOVA and expected sequence
  token. Any mismatch stops further writes.
- Cleanup proceeds only through confirmed responses. If a command may have
  executed but its response is missing, retain permanent buffer/table memory
  and the active SMMU translation until reboot.
- The mode is opt-in and must not change the behavior of modes 1–6.

## Success criteria

- Host tests verify Linux packet byte layouts, malformed/rejected responses,
  SMMU ownership selection and the finite state-machine limit of four writes.
- CI builds the complete ordered patch series without errors.
- A read-only `fastboot boot` capture shows the dedicated mapping, both amp
  resets low, successful ASM map/open, ADM open/route, PCM format, AFE start,
  RUN, four address-matched write completions, successful close/unmap and
  AFE stop, followed by restoration of SMMU routing.
- The phone remains unmodified at the partition level. No audio audibility or
  SpringBoard success is inferred from this diagnostic.
