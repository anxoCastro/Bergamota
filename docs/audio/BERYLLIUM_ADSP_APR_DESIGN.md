# Beryllium ADSP/APR bring-up design

## Purpose and first milestone

This design defines the first kernel-side audio milestone for ios-beryllium: start the SDM845 ADSP safely, establish its GLINK endpoint, open the APR audio service, and query the ADSP core service version. It stops at control-plane discovery. It does not create PCM streams, drive the WCD9340 or TAS2559/2560, or expose audio to iOS yet.

The later route work remains: Q6 AFE/ASM/ADM, WCD9340 over SLIMbus, QUAT MI2S and TAS amps, microphone selection/capture, jack events, call audio, and the iOS 26 audio provider. The Beryllium route inventory is in `NOTES.md`.

## Confirmed Beryllium topology

The facts below come from the upstream SDM845 and Beryllium device trees. They describe Linux's device configuration; the XNU implementation must still confirm the running device's memory and firmware state before writing or assigning anything.

| Layer | Beryllium value | Evidence |
|---|---|---|
| ADSP remote processor | `qcom,sdm845-adsp-pas`; PAS ID 1 in the Linux PAS driver | [Beryllium common DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845-xiaomi-beryllium-common.dtsi), [Linux Q6V5 PAS driver](https://github.com/torvalds/linux/blob/master/drivers/remoteproc/qcom_q6v5_pas.c) |
| Firmware name | `qcom/sdm845/Xiaomi/beryllium/adsp.mbn` | [Beryllium common DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845-xiaomi-beryllium-common.dtsi) |
| ADSP reserved memory | `0x8c500000` + `0x01e00000` (30 MiB) | [Beryllium common DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845-xiaomi-beryllium-common.dtsi) |
| SMP2P | SMEM items `<443>, <429>`; AP local PID 0, ADSP remote PID 2; SMP2P mailbox client 10 | [SDM845 DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845.dtsi) |
| ADSP GLINK edge | remote PID 2; APSS shared mailbox client 8; channel `apr_audio_svc` | [SDM845 DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845.dtsi) |
| APR services | Q6 core 3, AFE 4, ASM 7, ADM 8; ASM DAI uses Apps SMMU SID `0x1821` | [SDM845 DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845.dtsi) |

The generic SDM845 DTS has a different ADSP memory reservation; Beryllium deletes that node and supplies the 30 MiB region above. Use the Beryllium override. Do not copy the POCO X5 Pro memory values, firmware segmentation, or endpoint setup from HakiOS.

There are two different mailbox clients: SMP2P uses client 10 and the GLINK edge uses client 8. The existing modem path is separate: PID 1, client 12, and `IPCRTR`. Its constants and endpoint state must not be used for the ADSP.

## Startup flow

1. **Remain opt-in.** Add a `sweetadsp=1` boot argument, disabled by default, and start the worker from the existing Beryllium bring-up point after `rootvnode` is available. With the argument absent, do not load firmware or touch ADSP resources.
2. **Check firmware and memory before hardware changes.** Load the device's own `adsp.mbn` from the iOS root's `/usr/standalone/firmware/sweet/` directory, following the existing XNU firmware-loading pattern. Keep this Qualcomm/Xiaomi firmware outside Git. Inspect the actual artifact first: the Linux DTS gives a filename, while the Linux PAS path uses the MDT/PAS loader; that alone does not establish the on-device file format or segment layout.
3. **Respect existing DSP state.** Check whether the ADSP is already ready before attempting a cold start. If it is running, do not reset it or reclaim its memory blindly. If it is stopped, follow the SDM845 PAS sequence using PAS ID 1, the Beryllium reserved region, the required SCM memory assignment/authentication calls, and the board's documented clock/power resources. Every wait is bounded and each failed stage is logged.
4. **Establish readiness through SMP2P.** Use the ADSP's inbound/outbound SMEM items and remote PID 2. Record state transitions and wait for the expected ADSP-ready indication with a timeout. Do not assume the modem's `slave-kernel` state or item layout is a valid ADSP state machine.
5. **Open the ADSP GLINK edge.** Use remote PID 2, mailbox client 8, and `apr_audio_svc`. Discover and validate the correct SMEM descriptor/FIFO ownership and edge setup from the SDM845 transport; do not assume the modem's IDs 478–480 or its `IPCRTR` channel apply to this edge. A dedicated ADSP endpoint is the first implementation; only extract a shared transport after both endpoints are understood.
6. **Prove APR service discovery.** Open the Q6 core service (service 3) and issue `AVCS_GET_VERSIONS` (`0x00012905`). Validate packet length, response opcode, and service-count bounds before logging the firmware/service versions. Linux's [`q6core.c`](https://github.com/torvalds/linux/blob/master/sound/soc/qcom/qdsp6/q6core.c) provides the behavioral reference. An unsupported query is a logged capability result, not a kernel panic.
7. **Stop safely on failure.** If the ADSP was started by this driver and a later phase fails, shut it down through PAS when safe. If it was already running, leave it alone. A failure must not prevent iOS from continuing to boot.

The [Linux PAS driver](https://github.com/torvalds/linux/blob/master/drivers/remoteproc/qcom_q6v5_pas.c) is the reference for SDM845 resource ordering, remoteproc handover, memory assignment, and secure image loading. The existing XNU [GPU PAS implementation](../../xnu/patches-12377/0103-beryllium-gpu-a630-bringup.patch) demonstrates this fork's SMC calling conventions, but its PAS ID 13, GPU carve-out, and firmware parser are GPU-specific. The existing [MPSS GLINK implementation](../../xnu/patches-12377/1113-beryllium-mpss-rmtfs-qrtr-glink.patch) is similarly a protocol reference, not an ADSP implementation.

## XNU integration boundary

The first implementation belongs in a new `bsd/dev/beryllium_adsp.c` driver registered through `bsd/conf/files`, with the boot hook added beside the current Beryllium bring-up call in `bsd/dev/sweet_ufs.c`. Keep it isolated from `beryllium_wlan.c` and the MPSS driver's state. A small `beryllium_adsp_ipc.c`/header may own GLINK/APR if the first patch makes that split clearer.

The driver owns only firmware loading, ADSP PAS lifecycle, SMP2P readiness, GLINK/APR transport, and diagnostics. It does not access LPASS registers directly. [HakiOS's ADSP driver](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/xnu/files/bsd/dev/hakios.adsp.c) reports that its LPASS/I2S state is owned by the DSP; use the ADSP's AFE protocol once the APR control plane is understood. Its device-specific constants are not Beryllium values.

The first boot should produce clear `SWEET ADSP:` stage logs for firmware validation, existing/cold state, PAS result, SMP2P readiness, GLINK open, and APR core-version response. Keep verbose polling bounded. Do not add any analog route or PCM operation to this milestone.

## Evidence, inference, and unresolved points

**Observed:** Beryllium's Linux DTS enables the ADSP PAS node with the Xiaomi firmware path and a device-specific reserved region. The generic SDM845 topology supplies SMP2P, GLINK, mailbox, and APR service definitions. Linux's `qcom_q6v5_pas.c` uses the PAS ID 1 profile and handles firmware, reserved memory, remoteproc state, and GLINK subdevices. `q6core.c` queries ADSP service versions through APR. The local XNU tree has separate GPU PAS and modem GLINK precedents.

**Inference:** the first useful XNU milestone is an opt-in ADSP control-plane probe ending at a Q6 core APR response. This proves a substantial part of the audio stack before adding buffers, codecs, or iOS APIs, while keeping the existing modem and iOS boot paths isolated.

**Must be resolved before hardware boot:**

- The actual `adsp.mbn` artifact's format, segment layout, signing metadata, size, and compatibility with this device's secure monitor.
- The precise SDM845 PAS resource sequence and ownership state for a warm boot from the current iOS boot path.
- Which ADSP GLINK SMEM items are used on Beryllium, how Linux allocates/initializes them, and how to signal the edge safely.
- Whether service discovery requires additional ADSP protection-domain/PDR setup before the Q6 core responds.
- Whether `AVCS_GET_VERSIONS` is supported by the installed firmware and its response format/version.

Do not guess these values from HakiOS's Redwood port or from the modem driver. Record unknowns as runtime observations before adding later audio routes.

## Licensing boundary

Use Linux and HakiOS to identify protocol behavior, topology, and hardware facts. The local [LinuxKPI/licensing review](../LINUX_KCL.md) records the GPL boundary for Qualcomm audio. Implement the XNU driver independently; do not copy GPL ALSA/ASoC or remoteproc implementation code into this APSL tree. Retain provenance in documentation and credit upstream in `README.md` if code is later adapted, without co-author trailers.

## First-milestone acceptance

- Normal boot without `sweetadsp=1` performs no ADSP operation.
- An opt-in boot either reports a validated already-running DSP or completes the bounded PAS → SMP2P → GLINK → APR core-version sequence.
- Missing/invalid firmware, unavailable resources, timeout, unsupported APR query, or malformed reply is logged and leaves iOS booting.
- The implementation does not touch modem state, directly poke LPASS audio registers, create PCM buffers, or claim that any route works.
- Build and device evidence is recorded in `NOTES.md`: patch/commit, CI run ID, exact boot arguments, and observed log result.
