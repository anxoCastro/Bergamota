# Beryllium ADSP watchdog IRQ counter

## Intent

Find out whether Q6V5 asserts watchdog GIC SPI 162 during the five-second window after PAS `AUTH_AND_RESET`. The current GIC snapshot only reads the interrupt's enabled/pending/active bits at the end, so it cannot show whether an event was already acknowledged.

## Approaches

1. **Implement Linux's full Q6V5 event lifecycle.** This would add watchdog, fatal, ready, handover, and stop-ack handling together. It changes runtime behavior and depends on IRQ wiring that has not yet been verified in XNU.
2. **Count only SPI 162 in XNU's existing IRQ dispatch, then report the delta outside interrupt context.** This observes the event without changing GIC acknowledgement, PAS, SMEM, or SMP2P. This is the selected approach because it isolates one observable fact with the smallest runtime change.
3. **Keep the end-of-timeout GIC snapshot only.** This has no code risk, but it cannot distinguish an earlier watchdog interrupt from no interrupt.

## Goals and non-goals

- Count watchdog SPI 162 deliveries during the PAS start window and include the count delta in timeout/fatal diagnostics.
- Leave the existing generic XNU interrupt dispatch and GIC end-of-interrupt path intact.
- Do not change the remote-owned SMP2P item 443, PAS calls, shutdown policy, or GLINK/APR/audio behavior.
- Do not infer that a zero count proves the ADSP did not fail; this only measures delivery through the current Group-1 IRQ path.

## Constraints

- Keep `sweetadsp=2` opt-in and keep diagnostics read-only.
- Preserve the active Android/recovery/modem/persist partitions and existing backups. Device validation uses only temporary `fastboot boot`; no partition is flashed.
- The user authorized temporary rootrw diagnostic boots; the candidate uses the existing `userdata` root only.
- Per repository instructions, do not add co-author trailers. Validate with patch application, CI build/log review, and the authorized hardware boot.

## Implementation choices

Add a relaxed atomic counter in the existing `SWEET` branch of `osfmk/arm64/sleh.c` when its current `ICC_IAR1_EL1` read returns SPI 162. Expose a getter so `bsd/dev/beryllium_adsp.c` can snapshot the count immediately before `AUTH_AND_RESET` and report the delta after timeout/fatal. The interrupt is still passed to XNU's existing `cdp->interrupt_handler` and receives the same EOI write. Keep the code in a new ordered patch 1134; do not fold this diagnostic into the earlier PAS patch.

## Acceptance evidence

The XNU 26.5 CI log must show patch 1134 applied and a successful kernel build with empty `errors.txt`. A temporary image must preserve the known ADT marker and carry `ufsrw=1 rootrw=1 sweetadsp=2`. On Beryllium, the captured log must contain the SPI 162 count delta, the existing PAS/Q6V5 observations, and the mount/SpringBoard result. The meaning of the delta is limited to whether XNU's IRQ entry path saw SPI 162 during that window.
