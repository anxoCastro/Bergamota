# Beryllium ADSP PAS startup design

## Intent

Move from the completed, read-only ADSP preflight to a narrowly scoped attempt to start the Poco F1's ADSP through PAS ID 1. The goal is to observe the DSP's ready state and preserve a clear boundary before SMP2P writes, GLINK/APR, or audio. This document is a design for later kernel work; it does not authorize a PAS-enabled device boot.

## Current evidence

- The handset firmware bundle has been copied to `/usr/standalone/firmware/sweet/`; all 18 files were SHA-256 checked over SSH. Its MDT is 7,516 bytes, 16 ELF32 program headers, and the signed image spans the 30 MiB Beryllium ADSP carveout. The read-only kernel preflight accepts the MDT metadata. No PAS call has been made.
- The early read-only SMP2P snapshot found no ADSP-private item 443 and reported state 3 (`NOT_READY`). This does not prove whether the DSP is off, already active without that item, or in another state.
- The phone is currently in Fastboot; `fastboot getvar product` returned `beryllium`.
- The latest read-only boot reached the iOS root and the ADSP metadata probe, then stopped during launchd's `mount-phase-1`. Inspection of the embedded boot-task plist shows `/sbin/mount -P 1` has `RequireRun=true` and `RequireSuccess=true`. The read-only log ends after `exec mount_hfs`; the user then observed `mount[3] exited` and a kernel panic. A prior `rootrw=1` boot passed this phase, so root writability is the leading difference, but the read-only run has no final mount errno or exit status to prove the exact cause. Do not repeat a writable-root boot without fresh approval.

## Approaches considered

1. **Copy Linux's whole Q6V5/remoteproc stack into XNU.** This follows upstream closely but brings in unrelated remoteproc, PM, regulator, clock, interconnect, and firmware framework assumptions. It is too broad for the first PAS experiment.
2. **Add a small XNU-native PAS stage that follows Linux's Beryllium resource order.** Reuse only the existing XNU SCM calling convention where appropriate, and implement bounded MDT loading, state gates, and ready reporting for this device. This is the recommended approach because the fork already has a working PAS precedent for the GPU and a read-only ADSP preflight; it keeps this stage auditable and isolated.
3. **Continue read-only observations without attempting PAS.** This is safest but cannot answer whether the authenticated firmware starts the DSP or raises Q6V5 ready.

The recommendation is approach 2. The PAS operation itself remains behind a new explicit stage value and a fresh user-approved temporary boot.

## Scope

### Goals

- Add an opt-in PAS-only stage, distinct from existing `sweetadsp=1` read-only preflight behavior.
- Validate the complete split firmware bundle and its load bounds before the first write.
- Observe Q6V5 fatal, ready, handover, and stop-ack state; keep PAS disabled until an independent, documented stopped-state precondition exists.
- Follow the Beryllium-specific Linux order for PAS metadata, memory setup, segment loading, authentication/reset, and ready wait.
- Log each stage and return control to iOS on an error or timeout.

### Non-goals

- No writes to SMP2P outbound item 429 or remote inbound item 443.
- No GLINK edge initialization, APR service discovery, PDR/SERVREG requests, PCM, codec, amp, microphone, or audio HAL work.
- No changes to GPT or any partition; no firmware blobs, serials, or device dumps in Git.
- No copying GPL Linux remoteproc/ASoC code into XNU.

## Beryllium resource facts

| Resource | Beryllium value | Evidence and implementation implication |
|---|---|---|
| PAS identity | ADSP PAS ID 1 | Linux `sdm845_adsp_resource_init`; never reuse GPU PAS ID 13. |
| Firmware | `/usr/standalone/firmware/sweet/adsp.mdt` plus split `adsp.bNN` files | Handset-derived bundle; require every nonempty load sidecar to exist and match its ELF `p_filesz`. |
| Carveout | `0x8c500000`, size `0x01e00000` (30 MiB), `no-map` | Beryllium common DTS; current XNU RAM is documented as starting at `0x1_00000000`, and the ADSP range is also in the fixed low-pool denylist. Before writing or assigning it, check that the live ADT has the exact reservation and the active XNU physical-memory range remains disjoint. This proves allocator exclusion only, not hypervisor/TrustZone ownership or PAS permission. |
| Q6V5 interrupts | watchdog SPI 162; SMP2P inbound bits 0–3 named fatal, ready, handover, stop-ack | SDM845 DTS. Read current line/state before registering or clearing any event; do not destroy a pending state. |
| Required clock | `RPMH_CXO_CLK`, name `xo` | The SDM845 ADSP node declares this clock. |
| Load state | AOSS QMP name `adsp` | Linux Q6V5 prepare requests `load_state=on`; handover/unprepare later clears it. |
| Stop state | SMP2P outbound state `stop`, bit 0 | Linux uses this for orderly shutdown; this design does not set it during startup. |
| Linux PAS descriptor | crash-reason SMEM item 423; SSR `lpass`; sysmon `adsp`; SSCTL ID `0x14` | SDM845-specific Linux PAS data. Crash reason and system-monitor work are diagnostic/later work, not a reason to write arbitrary SMEM. |
| Optional resources | Generic Linux code can enable proxy domains, AGGRE2, and CX/PX supplies; SDM845 PAS data names no proxy power domains, and this ADSP DTS node declares no AGGRE2 clock or CX/PX supplies | Do not invent those resources. Use the Beryllium DTS and device-tree availability checks. |

Linux's generic start sequence also prepares Q6V5's load-state/IRQ context before PAS, turns on declared resources, loads/authenticates the MDT metadata, sets up relocatable memory, copies split segments and zero-fills BSS, calls AUTH_AND_RESET, then waits up to five seconds for ready. Its handover callback releases temporary proxy resources. For Beryllium, the generic optional resources above are absent from the specific node/configuration; the required XO, AOSS QMP, stop-state, IRQ, and carveout facts still need XNU-side availability checks.

The Q6V5 signals do **not** currently provide a proven pre-start “ADSP is stopped” tuple. In Linux, `ready` completes a start wait, `handover` releases startup votes after firmware handover, and `stop-ack` completes a stop request after Linux asserts the outbound stop bit. These event semantics do not establish that a zeroed or missing snapshot means the remote is off. The existing item-443 absence / state-3 observation is therefore insufficient. Until a documented reset/off condition can be distinguished from active, stale, or faulted state, the PAS stage must remain fail-closed before any secure call or carveout write. Do not infer safety from `fatal=0`, `ready=0`, `handover=0`, and `stop-ack=0` alone.

## Proposed stage behavior

1. **Remain opt-in.** Keep `sweetadsp=1` read-only. Reserve `sweetadsp=2` for PAS startup and make the default path perform no ADSP write or SMC. Emit a stage banner before any action.
2. **Take a no-write state snapshot.** Read the Q6V5 fatal, ready, handover, and stop-ack indications and existing SMP2P data without changing their state. Abort if fatal or ready is asserted, a signal cannot be read reliably, or state is ambiguous. A snapshot with all four indications clear is not a sufficient stopped-state proof. The first PAS implementation and device gate remain disabled until a separate documented reset/off condition establishes that starting cannot reset an active ADSP. Missing item 443 alone is not sufficient evidence that a cold start is safe.
3. **Gate the firmware and memory.** Validate that the SCM PAS call convention, including interrupted-call handling, is established for this XNU platform. Query PAS ID 1 support only through a validated SCM path; the support query is diagnostic unless its negative result is known to be authoritative on SDM845. Re-parse the full MDT with strict ELF32 bounds; verify the hash metadata bytes and all 13 nonempty segment files (`b02`–`b14`) against their expected sizes. Require the physical image extent to fit wholly inside `0x8c500000..0x8e300000`. Confirm the live ADT's exact fixed ADSP reservation and verify that XNU's active physical-memory range excludes it. This stage does not add or change a platform reservation; if either check fails, stop and handle the reservation in a separate platform change before PAS work. If any check fails, do not call INIT_IMAGE, MEM_SETUP, AUTH_AND_RESET, or SHUTDOWN.
4. **Prepare the Beryllium resources.** Establish Q6V5 ready/fatal/handover handling and the AOSS `adsp` load-state context. Enable the declared XO clock using the existing clock-controller path. Treat missing/unavailable resources as a clean stage failure; do not substitute guessed register writes or generic proxy-domain names.
5. **Load MDT metadata and segments.** Construct the authentication metadata as Linux `qcom_mdt_read_metadata()` does: ELF header followed immediately by the hash segment, sourced from the split MDT format. Place the temporary metadata in physically contiguous, suitably aligned memory and apply the required cache maintenance before SCM. Call PAS INIT_IMAGE for ID 1; for this relocatable image, call MEM_SETUP for the carveout and the exact loaded extent; then copy the bounded sidecars into the reserved region and zero each `p_memsz - p_filesz` tail.
6. **Authenticate and observe readiness.** Call PAS AUTH_AND_RESET for ID 1 once. Wait at most five seconds for Q6V5 ready while fatal remains clear. Log the SCM return/result and all observed ready/fatal transitions. If this start succeeded but ready times out, issue PAS SHUTDOWN for the instance started by this driver, then release temporary resources; never shut down an instance detected as already running.
7. **Stop at ADSP ready.** Do not initialize GLINK, APR, or audio after ready in this stage. Release temporary startup votes only at the Q6V5 handover point where Linux does so. Clear the AOSS load-state request and restore resources on pre-authentication failure. An error must not panic XNU or block the remaining boot.

## Risks and unresolved gates

- **DSP stopped state is unproven.** Linux's ready, handover, and stop-ack notifications describe lifecycle events, not a standalone pre-start off-state. No signal tuple is currently approved as proof. Keep PAS fail-closed until an independent, documented reset/off condition is available.
- **Physical ownership is only partly established.** The current XNU managed-memory base is documented above 4 GiB, and the low-pool denylist includes the ADSP range. At runtime, still verify the exact ADT reservation and physical-memory bounds before any carveout write. Those checks do not prove hypervisor/TrustZone ownership or PAS authorization. If the range overlaps XNU RAM or the ADT entry is missing/mismatched, this stage aborts; a reservation fix is a separate platform prerequisite.
- **SCM metadata semantics matter.** The preflight only validates ELF tables; it does not validate the TrustZone hash metadata, PAS support for ID 1, or authentication compatibility. These are runtime gates.
- **Warm-boot state is unknown.** Item 443 was absent in the early snapshot. Add read-only Q6V5 signal observation, but fail closed even when all sampled bits are clear until a reset/off condition distinguishes stopped from active/faulted states.
- **The current read-only boot later panics at mount-phase-1.** PAS diagnostics run before launchd, so the PAS result can be captured even if that later mount task still fails. Do not use another writable-root boot without fresh approval.
- **A PAS-enabled device test is a separate authorization.** Before it, review the generated patch/payload, verify it has no UFS/root-write flags and no partition-write operation, and ask for explicit approval to execute PAS ID 1. Use temporary `fastboot boot` only; preserve bootloader, recovery, modem, persist, GPT, and backups.

## Acceptance evidence for this stage

- Normal boot and `sweetadsp=1` remain read-only and perform no PAS operation.
- `sweetadsp=2` either fails a named precondition without writes, or reports successful PAS ID 1 authentication and Q6V5 ready within the bounded timeout.
- Logs prove no outbound SMP2P write, GLINK/APR open, or audio operation occurred.
- Any PAS error is logged and boot continues; no unexplained reset of an already-running DSP occurs.
- Exact build, payload hash, Fastboot result, readiness result, and any later mount failure are recorded in `NOTES.md`.

## Primary source references

- [Beryllium SDM845 device tree](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845-xiaomi-beryllium-common.dtsi)
- [SDM845 device tree ADSP/PAS, SMP2P, and GLINK resources](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845.dtsi)
- [Linux Qualcomm Q6V5 PAS driver](https://github.com/torvalds/linux/blob/master/drivers/remoteproc/qcom_q6v5_pas.c)
- [Linux Q6V5 state and ready handling](https://github.com/torvalds/linux/blob/master/drivers/remoteproc/qcom_q6v5.c)
- [Linux MDT loader](https://github.com/torvalds/linux/blob/master/drivers/soc/qcom/mdt_loader.c)
- [Linux Qualcomm SCM PAS calls](https://github.com/torvalds/linux/blob/master/drivers/firmware/qcom/qcom_scm.c)
- [XNU managed-memory and fixed low-pool layout](../../DISPLAY_HW.md)
- Existing XNU PAS convention precedent: [`0103-beryllium-gpu-a630-bringup.patch`](../../../xnu/patches-12377/0103-beryllium-gpu-a630-bringup.patch); its PAS ID and carveout are GPU-only.
