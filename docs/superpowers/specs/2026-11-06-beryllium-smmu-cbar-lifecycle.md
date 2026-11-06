# Beryllium SMMU CBAR lifecycle

## Intent

Make repeated private ADSP buffer mappings usable when the SDM845 Apps SMMU
does not return CBAR[4] to its original zero value after it has been configured.
Qualcomm Linux disables an unused context bank by clearing `SCTLR`; it does not
restore CBAR/CBA2R or other register snapshots.

## Observations and inference

- Captures 1186 and 1187 show CBAR[4] starting at zero, being configured to
  `0x1f300`, then continuing to read `0x1f300` after writes that try to restore
  zero. The writes to CBAR zero are not shown to take effect.
- The restore helper checks CBA2R before CBAR. The recorded first mismatch is
  CBAR offset `0x1010`, so the CBA2R readback matched its saved zero.
- XNU's console line cap cut the diagnostic before the numeric CBA2R readback.
- Qualcomm Linux's `arm_smmu_destroy_domain_context()` disables `SCTLR` and
  frees page tables; it does not clear CBAR/CBA2R.
- **Inference to test:** CBAR is persistent configuration after first use;
  CBAR `0x1f300` is safe to leave when the context bank is disabled, its stream
  match is invalidated, and the saved stream routing is restored.

## Approaches

1. Keep strict snapshot restoration and retain the route/tables forever on the
   first CBAR mismatch. This is the current safe behavior, but it makes the
   next ADSP audio session fail after the first teardown.
2. Follow Linux's lifecycle: leave CBAR configured, restore and verify the
   remaining bank state, then restore and verify S2CR/SMR. This accepts only
   CBAR zero or this driver's exact `0x1f300` value. **Selected:** it is the
   narrowest behavior supported by the observed hardware and Linux's teardown.
3. Reset the whole Apps SMMU to recreate the original snapshot. This can alter
   unrelated streams and is outside this audio driver's ownership.

## Requirements

- Never clear a noncanonical CBAR while selecting or restoring a context bank.
- Accept only CBAR zero or `0x1f300` for the private audio context bank.
- Still require the CB translation enable bit to read zero after teardown.
- Still require all saved non-CBAR context registers to read back, including
  CBA2R, TTBR0/TTBR1, TCR/TCR2, MAIR, ACTLR and SCTLR.
- Keep the stream match invalid while the context bank is disabled and flushed.
- Restore S2CR and SMR in their existing order and require both exact readbacks.
- If any required state is uncertain, preserve the existing failure path and
  do not claim the route was restored.
- Do not write any partition, userdata, GPT, bootloader, recovery, modem, or
  persist data. Device validation is RAM-only with `fastboot boot`.

## Non-goals

- No ADSP protocol, PCM, amplifier, GPIO, clock, or PAS changes.
- No relaxation of the TLB, SCTLR, TTBR, S2CR, or SMR checks.
- No attempt to prove audible output in this lifecycle experiment.

## Validation result (2026-11-06)

The host fake-MMIO regressions pass, including rejection of a noncanonical
candidate CBAR and acceptance of the persistent canonical value after restore.
The 1188 KDK-enabled XNU 26.5 kernel built successfully and a read-only
`fastboot boot` mode-7 run completed all four matched `WRITE_DONE` events and
logged successful SMR/context-bank restoration. The device capture omits the
numeric CBAR/CBA2R values, so it proves the restore gate passed, but does not
show whether this run encountered a sticky canonical CBAR. TAS resets stayed
low; no audible output was tested. See the current hardware evidence and next
audio-layer task in `NOTES.md`.
