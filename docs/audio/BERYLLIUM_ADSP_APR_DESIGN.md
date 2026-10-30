# Beryllium ADSP/APR bring-up design

## Purpose and first milestone

This design records the target first kernel-side audio milestone for ios-beryllium: start the SDM845 ADSP safely, establish its GLINK endpoint, open the APR audio service, and query the ADSP core service version. The first implementation is narrower: an opt-in, read-only ADSP preflight. It does not create PCM streams, drive the WCD9340 or TAS2559/2560, or expose audio to iOS yet.

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

1. **Remain opt-in.** Add a `sweetadsp=1` boot argument, disabled by default, and call the preflight from the existing Beryllium bring-up point after `rootvnode` is available. With the argument absent, do not load firmware or inspect ADSP resources.
2. **Read-only preflight first.** Read at most 64 KiB from the device's `adsp.mbn` at `/usr/standalone/firmware/sweet/` and validate ELF32 header/program-header bounds. Validate any declared section-header table against the full file size. Follow Qualcomm MDT's image-wide split rule: if any nonempty program-header file range falls outside the `.mbn`, classify the image as split and report `.bNN` sidecars as expected for every nonempty non-hash `PT_LOAD` payload. Hash metadata uses a separate path and its source is not classified by this preflight. It does not open sidecars or read segment bytes, so their existence and size remain unverified; it must not claim firmware compatibility or load segments until the device artifact is checked. This follows upstream [`mdt_loader.c`](https://github.com/torvalds/linux/blob/master/drivers/soc/qcom/mdt_loader.c). Keep firmware outside Git.
3. **Inspect remote SMP2P state without modifying it.** The SDM845 DTS defines inbound item 443 and outbound item 429 for local PID 0 / remote PID 2. The first preflight may validate the SMEM table/private partition and read remote-owned item 443, reporting `slave-kernel` fatal/ready/handover/stop-ack bits. It must not allocate or write outbound item 429. A ready bit is an observation, not proof that memory reassignment or reset is safe.
4. **Defer PAS startup.** The upstream PAS remoteproc path enables proxy power domains, XO/AGGRE2 clocks, optional supplies, and secure image context before authentication. Beryllium's firmware, current ADSP state, reserved-memory ownership, and exact XNU resource controls have not been inspected on-device. Do not call PAS ID 1, assign `0x8c500000 + 0x01e00000`, or reset LPASS until those facts are observed and documented.
### Later control-plane stages (not in the first patch)

5. **Open the ADSP GLINK edge.** Use remote PID 2, mailbox client 8, and `apr_audio_svc`. Linux mainline's generic GLINK-SMEM transport uses SMEM items 478–480 for its descriptor and FIFOs on each edge; its SDM845 ADSP edge supplies remote PID 2. Validate the live descriptor, FIFO sizes, indices, and processor state before any writes. The modem's `IPCRTR` channel and live state remain separate; do not share the modem driver's transport state with ADSP. A dedicated ADSP endpoint is the first implementation; only extract a shared transport after both endpoints are understood.
6. **Prove APR service discovery.** Open the Q6 core service (service 3) after its protection domain is available. Linux's [`q6core.c`](https://github.com/torvalds/linux/blob/master/sound/soc/qcom/qdsp6/q6core.c) first requests `AVCS_CMD_GET_FWK_VERSION` (`0x001292c`) and falls back to `AVCS_GET_VERSIONS` (`0x00012905`) when the former is unsupported. For an initial XNU probe, select the query after checking the installed firmware; validate response opcode, minimum packet size, and service-count-derived length before reading any versions. Unsupported queries and unavailable protection domains are logged capability results, not kernel panics.
7. **Stop safely on failure.** If the ADSP was started by this driver and a later phase fails, shut it down through PAS when safe. If it was already running, leave it alone. A failure must not prevent iOS from continuing to boot.

The [Linux PAS driver](https://github.com/torvalds/linux/blob/master/drivers/remoteproc/qcom_q6v5_pas.c) is the reference for SDM845 resource ordering, remoteproc handover, memory assignment, and secure image loading. Its startup enables proxy power domains and clocks before authentication; copying only the PAS SMC sequence would be incomplete. The existing XNU [GPU PAS implementation](../../xnu/patches-12377/0103-beryllium-gpu-a630-bringup.patch) demonstrates this fork's SMC calling conventions, but its PAS ID 13, GPU carve-out, and firmware parser are GPU-specific. The existing [MPSS GLINK implementation](../../xnu/patches-12377/1113-beryllium-mpss-rmtfs-qrtr-glink.patch) is similarly a protocol reference, not an ADSP implementation.

## Linux GLINK/APR source findings

These are facts about Linux mainline's SDM845 configuration and generic drivers, not observations of the phone's current ADSP runtime state.

| Topic | Linux source finding | What remains unproven on Beryllium |
|---|---|---|
| ADSP edge | `sdm845.dtsi` defines the ADSP `glink-edge` with remote PID 2, APSS mailbox client 8, edge IRQ 156, label `lpass`, and APR channel `apr_audio_svc`. `qcom_common.c` attaches this node as a remoteproc subdevice and starts it with `qcom_glink_smem_register()`. | Whether the installed Android firmware and the current iOS boot state have this edge initialized, and whether Linux's generic transport settings match the live firmware version. |
| SMEM item IDs | `qcom_glink_smem.c` defines descriptor item 478, FIFO items 479 and 480, and passes the edge's remote PID (2 here) to the SMEM allocation/get calls. This establishes 478–480 as the mainline Linux transport reference for the ADSP edge as well as other GLINK-SMEM edges. | The actual partition/table mapping, item contents, memory attributes, and live producer/consumer indices. Passing a host ID into SMEM does not establish physical ownership or prove a given item is safe to reset. |
| Descriptor and FIFOs | Linux accepts an existing descriptor item or allocates a 32-byte one, requires the retrieved descriptor to be exactly 32 bytes, binds descriptor words 0–3 as TX tail/head and RX tail/head, allocates or accepts FIFO 479 at 16 KiB for TX, and retrieves FIFO 480 lazily for RX without allocating it in that path. | Whether these sizes and allocations match the phone's live items. The source code does not prove ownership of each descriptor word or FIFO for a warm boot into XNU. |
| Initialization and doorbell | Before enabling the edge IRQ, Linux sets the RX tail and TX head indices to zero. Native GLINK begins version/features negotiation (version 1, intent-reuse feature); channel opening then uses the negotiated GLINK open/open-ack protocol. | Resetting indices in XNU could corrupt an active endpoint. Confirm remote state and define safe teardown/ownership before writing indices, FIFO data, or mailbox doorbells. |
| Receive intents | The DTS declares `qcom,intents = <512 20>` on the APR node. The reviewed GLINK-SMEM/native transport and APR paths establish negotiated intents and their use for receiving packets, but do not establish the exact meaning/consumer of this DTS pair for this edge. | Do not treat `<512 20>` as a confirmed buffer-size/count contract until the actual driver/version consuming it is identified. Runtime intent announcements and packet limits remain to be observed. |
| APR protection domains | The SDM845 DTS declares domain `APR_DOMAIN_ADSP`; Q6 core/AFE/ASM/ADM services are IDs 3/4/7/8 and carry the protection-domain pair `avs/audio`, `msm/adsp/audio_pd`. Linux APR registers PDR lookups and makes those APR service devices available when the matching service path reports UP. | Whether the Beryllium firmware exposes the same PDR name/path and whether a PDR/SERVREG service is already active in the current boot. |

Linux's `q6core.c` represents `AVCS_GET_VERSIONS_RSP` as a fixed build ID and service count followed by service/version entries; the newer firmware-version response has a different per-service record. The XNU probe must validate the response opcode, header/payload lengths, and bounded count for whichever query it sends. Linux's timeout and fallback behavior are useful protocol references, not a guarantee that this phone's firmware supports either query.

## XNU integration boundary

The first implementation belongs in a new `bsd/dev/beryllium_adsp.c` driver registered through `bsd/conf/files`, with the boot hook added beside the current Beryllium bring-up call in `bsd/dev/sweet_ufs.c`. Keep it isolated from `beryllium_wlan.c` and the MPSS driver's state. A small `beryllium_adsp_ipc.c`/header may own GLINK/APR if the first patch makes that split clearer.

The eventual driver may own firmware loading, ADSP PAS lifecycle, SMP2P, GLINK/APR transport, and diagnostics. The first patch owns only bounded firmware-metadata and read-only SMP2P inspection. It does not access LPASS registers directly. [HakiOS's ADSP driver](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/xnu/files/bsd/dev/hakios.adsp.c) reports that its LPASS/I2S state is owned by the DSP; use the ADSP's AFE protocol once the APR control plane is understood. Its device-specific constants are not Beryllium values.

The first preflight should produce clear `SWEET ADSP:` logs for ELF metadata bounds, embedded versus expected image-wide split non-hash payloads, the unclassified hash-metadata source, and the read-only SMP2P snapshot. An expected `.bNN` sidecar is not evidence that it exists or has the required size. A later PAS/GLINK/APR boot can add those stage results after firmware, ownership, power, and transport details are observed. Do not add any analog route or PCM operation to this milestone.

## Evidence, inference, and unresolved points

**Observed:** Beryllium's Linux DTS enables the ADSP PAS node with the Xiaomi firmware path and a device-specific reserved region. The generic SDM845 topology supplies SMP2P, GLINK, mailbox, and APR service definitions. Linux's `qcom_q6v5_pas.c` enables proxy power domains, XO/AGGRE2 clocks, optional CX/PX supplies, and secure image handling. Its ADSP DTS maps SMP2P item 443 to remote `slave-kernel`, with interrupt names `fatal`, `ready`, `handover`, and `stop-ack`; item 429 is AP-owned outbound state. `mdt_loader.c` handles ELF32 metadata with embedded or split segment data. `q6core.c` queries ADSP service versions through APR. A local scan found no `adsp.mbn` or `dsp.img`, and there was no authorized ADB device available to retrieve one; this establishes only that the artifact is currently unavailable in the workspace, not that the phone lacks it. The local XNU tree has separate GPU PAS and modem GLINK precedents.

**Inference:** the first implementation should stop at an opt-in read-only preflight. Without the actual firmware and runtime processor/memory state, a cold start could reset a running DSP or alter memory ownership. A later PAS→SMP2P→GLINK→APR milestone remains the target once the missing observations are available.

**Must be resolved before hardware boot:**

- The actual `adsp.mbn` artifact's format, segment layout, signing metadata, size, and compatibility with this device's secure monitor.
- The phone's existing ADSP state and the current owner/permissions of the reserved-memory region.
- The precise SDM845 PAS resource sequence and ownership state for a warm boot from the current iOS boot path, including XO/AGGRE2 clocks and proxy power domains.
- Whether the live Beryllium ADSP edge has Linux's SMEM items 478–480, what their actual sizes/indices and ownership are, and whether the edge is already active. Linux mainline's reference allocation and initialization are now traced, but not checked against the phone.
- Whether Beryllium exposes the Linux-configured `avs/audio` → `msm/adsp/audio_pd` protection domain and its runtime PDR state before Q6 core requests are sent.
- Whether `AVCS_GET_VERSIONS` is supported by the installed firmware and its response format/version.

Do not guess these values from HakiOS's Redwood port or from the modem driver. Record unknowns as runtime observations before adding later audio routes.

## Licensing boundary

Use Linux and HakiOS to identify protocol behavior, topology, and hardware facts. The local [LinuxKPI/licensing review](../LINUX_KCL.md) records the GPL boundary for Qualcomm audio. Implement the XNU driver independently; do not copy GPL ALSA/ASoC or remoteproc implementation code into this APSL tree. Retain provenance in documentation and credit upstream in `README.md` if code is later adapted, without co-author trailers.

## First-milestone acceptance

- Normal boot without `sweetadsp=1` performs no ADSP operation.
- An opt-in preflight validates ELF header/program-header and declared section-table bounds, classifies the image as embedded or split using the Qualcomm MDT rule, reports expected but unverified `.bNN` sidecars for non-hash payloads, leaves hash-metadata source unclassified, and reads only the remote-owned SMP2P item.
- No PAS call, memory assignment, SMP2P outbound write, GLINK/APR operation, or LPASS register write occurs in this first implementation.
- Preflight failures are logged and leave iOS booting; no audio route is claimed to work.
- Build and device evidence is recorded in `NOTES.md`; firmware and dumps stay outside Git.
