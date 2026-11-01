# Beryllium ADSP crash-reason timing

## Intent

Determine when ADSP global SMEM item 423 first appears or changes after PAS `AUTH_AND_RESET`, so the next Beryllium boot can distinguish a pre-existing crash string from one published during startup.

## Goals and constraints

- Sample item 423 in read-only mode throughout the existing five-second Q6V5 ready window.
- Log the first observed time and sanitized text, plus the first later text change.
- Keep the current PAS sequence, SMP2P ownership, IRQ handling, and timeout behavior unchanged.
- Do not write SMEM, issue PAS `SHUTDOWN`, or start GLINK/APR/audio until item 443 contains a valid `slave-kernel` entry.
- Treat the SFR string as an ambiguous subsystem-failure message; timing does not by itself prove a watchdog bite or identify the cause.

## Approaches

- **End snapshot only:** smallest code change, but does not locate when the item appears. Rejected because the prior boot already has an end snapshot.
- **Full Q6V5 lifecycle handlers:** could expose fatal/ready events, but changes control flow and depends on IRQ wiring not yet verified. Deferred.
- **Read-only item-423 sampling during the existing wait:** recommended and selected by the implementer under the user's standing instruction to continue autonomously. It adds only bounded observations to the already opt-in PAS experiment.

## Implementation shape

Refactor the existing bounded item-423 read into a helper used by both the pre/post snapshots and the Q6V5 wait loop. Sample on each existing 10 ms loop iteration, emit at most one first-appearance record and one first-change record, and continue to sanitize/truncate the text before logging. The build and temporary image remain on the `audio-poco-f1-adsp-bootstrap` branch. A hardware boot uses `fastboot boot` only after Fastboot enumeration is confirmed.

## Non-goals

This change will not infer the ADSP failure cause, change firmware loading or memory assignment, add event interrupts, or implement audio routes.
