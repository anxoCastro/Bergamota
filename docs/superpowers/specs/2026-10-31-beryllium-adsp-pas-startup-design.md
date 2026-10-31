# Beryllium ADSP PAS startup design

## Intent

Move from the completed, read-only ADSP preflight to a narrowly scoped attempt to start the Poco F1's ADSP through PAS ID 1. The goal is to observe the DSP's ready state and preserve a clear boundary before SMP2P writes, GLINK/APR, or audio. The user authorized temporary phone experiments on 2026-10-31, including an explicitly selected PAS start; this does not authorize partition flashing or filesystem repair.

## Current evidence

- The handset firmware bundle has been copied to `/usr/standalone/firmware/sweet/`; all 18 files were SHA-256 checked over SSH. Its MDT is 7,516 bytes, 16 ELF32 program headers, and the signed image spans the 30 MiB Beryllium ADSP carveout. The read-only kernel preflight accepts the MDT metadata. No PAS call has been made.
- The early read-only SMP2P snapshot found no ADSP-private item 443 and reported state 3 (`NOT_READY`). This does not prove whether the DSP is off, already active without that item, or in another state.
- The phone is currently in Fastboot; this turn's `fastboot getvar product` returned `beryllium`.
- The captured Android FDT contains `/reserved-memory/memory@8c500000` with base `0x8c500000` and size `0x01e00000`. The iOS ADT passed to XNU does not contain this reservation, so temporary PAS payloads must carry the verified board range in `/chosen/sweet-adsp-region`; XNU must also verify it does not overlap `gPhysBase/gPhysSize`.
- The latest read-only boot reached the iOS root and the ADSP metadata probe, then stopped during launchd's `mount-phase-1`. Inspection of the embedded boot-task plist shows `/sbin/mount -P 1` has `RequireRun=true` and `RequireSuccess=true`. The read-only log ends after `exec mount_hfs`; the user then observed `mount[3] exited` and a kernel panic. A prior `rootrw=1` boot passed this phase, so root writability is the leading difference, but the read-only run has no final mount errno or exit status to prove the exact cause. Do not repeat a writable-root boot without fresh approval.

## Approaches considered

1. **Copy Linux's whole Q6V5/remoteproc stack into XNU.** This follows upstream closely but brings in unrelated remoteproc, PM, regulator, clock, interconnect, and firmware framework assumptions. It is too broad for the first PAS experiment.
2. **Add a small XNU-native PAS stage that follows Linux's Beryllium resource order.** Reuse only the existing XNU SCM calling convention where appropriate, and implement bounded MDT loading, state gates, and ready reporting for this device. This is the recommended approach because the fork already has a working PAS precedent for the GPU and a read-only ADSP preflight; it keeps this stage auditable and isolated.
3. **Continue read-only observations without attempting PAS.** This is safest but cannot answer whether the authenticated firmware starts the DSP or raises Q6V5 ready.

The recommendation is approach 2. The PAS operation remains behind `sweetadsp=2`; ordinary boot and `sweetadsp=1` remain read-only.

## Scope

### Goals

- Add an opt-in PAS-only stage, distinct from existing `sweetadsp=1` read-only preflight behavior.
- Validate the complete split firmware bundle and its load bounds before the first write.
- Observe Q6V5 fatal, ready, handover, and stop-ack state. If the read-only snapshot reports ready or fatal, refuse to start. If SMP2P is unavailable, log that physical ADSP state is unknown; `sweetadsp=2` is the operator's explicit request to attempt the PAS start and may reset the DSP.
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
| Q6V5 interrupts | watchdog SPI 162; SMP2P inbound bits 0–3 named fatal, ready, handover, stop-ack | SDM845 DTS. Patch 1132 reads only SMP2P bits; it does not observe watchdog SPI 162. No timeout cleanup may issue SHUTDOWN until that separate fatal channel is handled. |
| Required clock | `RPMH_CXO_CLK`, name `xo` | The SDM845 ADSP node declares this clock. |
| Load state | AOSS QMP name `adsp` | Linux Q6V5 prepare requests `load_state=on`; unprepare/stop clears it. This stage leaves it on after ready because it does not implement handover or stop. |
| Stop state | SMP2P outbound state `stop`, bit 0 | Linux uses this for orderly shutdown; this stage neither sets it nor calls PAS SHUTDOWN. |
| Linux PAS descriptor | crash-reason SMEM item 423; SSR `lpass`; sysmon `adsp`; SSCTL ID `0x14` | SDM845-specific Linux PAS data. Crash reason and system-monitor work are diagnostic/later work, not a reason to write arbitrary SMEM. |
| Optional resources | Generic Linux code can enable proxy domains, AGGRE2, and CX/PX supplies; SDM845 PAS data names no proxy power domains, and this ADSP DTS node declares no AGGRE2 clock or CX/PX supplies | Do not invent those resources. Use the Beryllium DTS and device-tree availability checks. |

Linux's generic start sequence also prepares Q6V5's load-state/IRQ context before PAS, turns on declared resources, sends INIT_IMAGE with ELF-header-plus-hash metadata, sets up relocatable memory, copies split segments and zero-fills BSS, calls AUTH_AND_RESET, then waits up to five seconds for ready. Its handover callback releases temporary proxy resources after the remote's handover event; unprepare/stop clears AOSS load-state. For Beryllium, the generic optional resources above are absent from the specific node/configuration. The ADSP requests RPMH_CXO_CLK (`xo`), whose SDM845 RPMh clock uses resource `xo.lvl` and exact on value `3`; the current XNU ARC max-vote API is not equivalent. This stage will send the verified `xo.lvl=3` vote and retain it after ready because it has no Q6V5 handover handler.

The Q6V5 signals do **not** currently provide a proven pre-start “ADSP is stopped” tuple. In Linux, `ready` completes a start wait, `handover` releases startup votes after firmware handover, and `stop-ack` completes a stop request after Linux asserts the outbound stop bit. These event semantics do not establish that a zeroed or missing snapshot means the remote is off. The existing item-443 absence / state-3 observation is inconclusive. Do not describe it as proof of a cold state. For the authorized experiment, a clear ready/fatal SMP2P read permits the explicit `sweetadsp=2` request to proceed while logging state as unknown; an asserted ready or fatal indication aborts. This stage does not observe the separate watchdog SPI 162, so it also does not call PAS SHUTDOWN after a fatal observation or ready timeout.

The TWRP Linux sysfs value `subsys4/state = OFFLINING` is also not a hardware power-state observation. In the inspected Qualcomm `subsystem_restart.c`, the enum's first value is `SUBSYS_OFFLINING` (zero), and the subsystem descriptor is zero-initialized before a lifecycle transition updates it. Treat that label as the driver's initial software tracker value; it does not establish that ADSP was physically off.

## Proposed stage behavior

1. **Remain opt-in.** Keep `sweetadsp=1` read-only. Reserve `sweetadsp=2` for PAS startup and make the default path perform no ADSP write or SMC. Emit a stage banner before any action.
2. **Take a no-write state snapshot.** Read the available SMP2P `slave-kernel` state without changing it. Abort if SMP2P ready or fatal is asserted. This stage does not observe watchdog SPI 162, handover, or stop-ack; if SMP2P is missing or unreadable, log physical state as unknown and continue only because `sweetadsp=2` explicitly requests this experimental start. A clear snapshot is not proof of a cold/off state.
3. **Gate the firmware and memory.** Validate that the SCM PAS call convention, including interrupted-call handling, is established for this XNU platform. Query PAS ID 1 support through that path. Re-parse the full MDT with strict ELF32 bounds; verify the hash metadata bytes and all 13 nonempty segment files (`b02`–`b14`) against their expected sizes. Require the physical image extent to fit wholly inside `0x8c500000..0x8e300000`. The test ADT must carry the exact `/chosen/sweet-adsp-region` marker generated from the captured Linux FDT reservation; verify it and ensure XNU's active physical-memory range excludes it. If any check fails, do not call INIT_IMAGE, MEM_SETUP, or AUTH_AND_RESET.
4. **Prepare the Beryllium resources.** Establish the AOSS `adsp` load-state context and enable the declared XO clock using the existing clock-controller path. SMP2P ready/fatal are sampled read-only; watchdog SPI 162 is not registered or cleared by this stage. Treat missing/unavailable QMP/XO resources as a clean pre-authentication failure; do not substitute guessed register writes or generic proxy-domain names.
5. **Load MDT metadata and segments.** Construct the authentication metadata as Linux `qcom_mdt_read_metadata()` does: ELF header followed immediately by the hash segment, sourced from the split MDT format. Place the temporary metadata in physically contiguous, suitably aligned memory and apply the required cache maintenance before SCM. Call PAS INIT_IMAGE for ID 1; for this relocatable image, call MEM_SETUP for the carveout and the exact loaded extent; then copy the bounded sidecars into the reserved region and zero each `p_memsz - p_filesz` tail.
6. **Authenticate and observe readiness.** Call PAS AUTH_AND_RESET for ID 1 once. Wait at most five seconds for Q6V5 ready and abort the wait if the SMP2P fatal bit is observed. Log the SCM return/result and all observed ready/fatal transitions. Watchdog SPI 162 is a separate fatal channel and is not monitored by this stage; on fatal or timeout, do not issue PAS SHUTDOWN and retain QMP/XO because the final remote state is unresolved.
7. **Stop at ADSP ready.** Do not initialize GLINK, APR, or audio after ready in this stage. Keep the AOSS load-state request and XO vote asserted after ready because Linux retains them until unprepare/handover and this stage does not implement that lifecycle. On a failure before successful AUTH_AND_RESET, request QMP load-state off if it had been enabled; retain the XO vote conservatively rather than guessing that another user does not need it. An error must not panic XNU or block the remaining boot.

## Implementation details in patch 1132

- The complete MDT and required sidecars are read into bounded buffers before the first PAS call. Each nonempty sidecar must equal its ELF `p_filesz`; the aggregate input cap is 128 MiB and each file is capped at 64 MiB. The MDT header-plus-hash buffer is built using the phone's split-file layout and remains limited to 64 MiB.
- The ADSP carveout is mapped write-combined only after the exact ADT marker, disjoint XNU RAM, PAS ID 1 support, AOSS QMP load-state, and XO vote checks pass. XNU zeroes the carveout and copies only validated segment bytes; the untouched tails remain zero.
- QMP maps its message RAM and doorbell on demand and reuses the MSS QMP channel if it is already open. `xo.lvl` is looked up as an RPMh ARC resource and receives the verified raw value `3`; this stage deliberately has no XO-off call.
- A conflicting duplicate `slave-kernel` item refuses PAS. Missing/unreadable SMP2P remains logged as unknown under the explicit mode-2 authorization. After AUTH_AND_RESET, polling remains read-only and is bounded to five seconds; because watchdog SPI 162 is not observed, the stage never calls PAS SHUTDOWN and retains QMP/XO after fatal or timeout.
- No outbound SMP2P item, GLINK, APR, or audio endpoint is touched. The implementation has local static contract tests; the remote XNU build and phone result are recorded separately in `NOTES.md`.

## Risks and unresolved gates

- **DSP stopped state is unproven.** Linux's ready, handover, and stop-ack notifications describe lifecycle events, not a standalone pre-start off-state. The authorized `sweetadsp=2` experiment may intentionally reset an unknown DSP state; it must still refuse if ready or fatal is observed.
- **Physical ownership is only partly established.** The current XNU managed-memory base is documented above 4 GiB, and the low-pool denylist includes the ADSP range. The captured Linux FDT contains the fixed board reservation; the iOS ADT does not, so the temporary PAS payload must add a matching marker and XNU must check the physical-memory bounds before any carveout write. Those checks do not prove hypervisor/TrustZone ownership or PAS authorization.
- **SCM metadata semantics matter.** The preflight only validates ELF tables; it does not validate the TrustZone hash metadata, PAS support for ID 1, or authentication compatibility. These are runtime gates.
- **Warm-boot state is unknown.** Item 443 was absent in the early snapshot. Repeat a read-only SMP2P check immediately before PAS. If it reports ready or fatal, abort. Missing/malformed SMP2P remains unknown and may proceed only under the user's explicit `sweetadsp=2` request, which accepts the possibility of resetting an unobserved active DSP. Watchdog SPI 162 is not observed, so the stage cannot establish that all fatal channels are clear and must not issue SHUTDOWN after timeout.
- **The current read-only boot later panics at mount-phase-1.** PAS logs are emitted before launchd, so the result can be captured even if that later mount task still fails. Keep `ufsrw=1` and `rootrw=1` out of the PAS payload. Use temporary `fastboot boot` only; preserve bootloader, recovery, modem, persist, GPT, and backups.

## Acceptance evidence for this stage

- Normal boot and `sweetadsp=1` remain read-only and perform no PAS operation.
- `sweetadsp=2` either fails a named precondition without writes, reports PAS ID 1 authentication and Q6V5 ready within the bounded timeout, or logs the timeout while retaining QMP/XO and issuing no SHUTDOWN.
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
- [Qualcomm Linux subsystem restart state tracker](https://android.googlesource.com/kernel/msm/%2B/android-msm-bluecross-4.9-pie-qpr1/drivers/soc/qcom/subsystem_restart.c)
- [XNU managed-memory and fixed low-pool layout](../../DISPLAY_HW.md)
- Existing XNU PAS convention precedent: [`0103-beryllium-gpu-a630-bringup.patch`](../../../xnu/patches-12377/0103-beryllium-gpu-a630-bringup.patch); its PAS ID and carveout are GPU-only.
