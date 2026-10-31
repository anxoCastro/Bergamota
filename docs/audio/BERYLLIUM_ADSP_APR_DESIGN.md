# Beryllium ADSP/APR bring-up design

## Purpose and first milestone

This design records the target first kernel-side audio milestone for ios-beryllium: start the SDM845 ADSP safely, establish its GLINK endpoint, open the APR audio service, and query the ADSP core service version. The first implementation is narrower: an opt-in, read-only ADSP preflight. It does not create PCM streams, drive the WCD9340 or TAS2559/2560, or expose audio to iOS yet.

The later route work remains: Q6 AFE/ASM/ADM, WCD9340 over SLIMbus, QUAT MI2S and TAS amps, microphone selection/capture, jack events, call audio, and the iOS 26 audio provider. The Beryllium route inventory is in `NOTES.md`.

## Current implementation status (2026-10-31)

Patch 1132 keeps ordinary boot and `sweetadsp=1` read-only and adds an explicit `sweetadsp=2` PAS ID 1 startup stage. In CI run 41223692918, the temporary writable-root image reached SpringBoard; the kernel log shows PAS `INIT_IMAGE`, `MEM_SETUP`, and `AUTH_AND_RESET` all returned status/result 0. Q6V5 did not publish a ready signal within five seconds, and inbound item 443 remained unavailable. The older `PAS_IS_SUPPORTED` result of false was diagnostic only; Linux's Beryllium path does not require that query before its PAS start sequence. The root mounted read-write and the earlier permanent-map panic did not recur. This rules out root write permission as the cause of that `vm_map_delete` panic.

The next local revision adds AP-owned SMP2P outbound item 429 and mailbox client 10 before PAS. It refuses malformed inbound state or any asserted `fatal`, `ready`, `handover`, or `stop-ack` bit, and verifies the mailbox mapping before changing item 429. This revision is not yet CI-built or booted. It still stops before GLINK/APR/audio.

## Confirmed Beryllium topology

The facts below come from the upstream SDM845 and Beryllium device trees. They describe Linux's device configuration; the XNU implementation must still confirm the running device's memory and firmware state before writing or assigning anything.

| Layer | Beryllium value | Evidence |
|---|---|---|
| ADSP remote processor | `qcom,sdm845-adsp-pas`; PAS ID 1 in the Linux PAS driver | [Beryllium common DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845-xiaomi-beryllium-common.dtsi), [Linux Q6V5 PAS driver](https://github.com/torvalds/linux/blob/master/drivers/remoteproc/qcom_q6v5_pas.c) |
| Firmware name in Linux package | `qcom/sdm845/Xiaomi/beryllium/adsp.mbn` | [Beryllium common DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845-xiaomi-beryllium-common.dtsi); postmarketOS's packaging patch says it squashes MDT plus `.bNN` files with `pil-squasher` for the `.mbn` form ([patch](https://lkml.iu.edu/2203.3/05982.html)) |
| Firmware files on this handset | modem partition `/image/adsp.mdt` plus `/image/adsp.b00`–`adsp.b14`; `adspr.jsn` and `adspua.jsn` describe ADSP services | Read-only TWRP observation: `/vendor/etc/fstab.qcom` mounts the `modem` VFAT partition at `/vendor/firmware_mnt`; files are under `/vendor/firmware_mnt/image/` |
| ADSP reserved memory | `0x8c500000` + `0x01e00000` (30 MiB) | [Beryllium common DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845-xiaomi-beryllium-common.dtsi) |
| SMP2P | SMEM items `<443>, <429>`; AP local PID 0, ADSP remote PID 2; SMP2P mailbox client 10 | [SDM845 DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845.dtsi) |
| ADSP GLINK edge | remote PID 2; APSS shared mailbox client 8; channel `apr_audio_svc` | [SDM845 DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845.dtsi) |
| APR services | Q6 core 3, AFE 4, ASM 7, ADM 8; ASM DAI uses Apps SMMU SID `0x1821` | [SDM845 DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845.dtsi) |

The generic SDM845 DTS has a different ADSP memory reservation; Beryllium deletes that node and supplies the 30 MiB region above. Use the Beryllium override. Do not copy the POCO X5 Pro memory values, firmware segmentation, or endpoint setup from HakiOS.

The handset's native image is a split MDT, not a single `.mbn`. Read-only extraction to the ignored local directory `.superpowers/firmware/beryllium/` found `adsp.mdt` at 7,516 bytes (ELF32 little-endian Qualcomm DSP6, 16 program headers, entry `0x8c500000`). Its 13 nonempty non-hash `PT_LOAD` payloads are split into `adsp.b02` through `adsp.b14`; each file size exactly matches its program header's `p_filesz`. Header index 15 has `p_filesz=0` and `p_memsz=0x4b7000`, so no `.b15` file is expected. `adsp.mdt` is byte-for-byte `adsp.b00` followed by `adsp.b01` (ELF header and hash data). `adspr.jsn` declares `root_pd`, QMI instance 74; `adspua.jsn` declares `audio_pd`, QMI instance 74, including the `avs/audio` service. The PT_LOAD addresses span `0x8c500000` to `0x8e300000` (30 MiB), matching the Beryllium carve-out; their flags mark the image relocatable. These file-layout matches confirm packaging consistency, not PAS authentication compatibility or permission to start the ADSP. The `dsp` partition is separate ext4 content containing DSP service modules; the boot firmware images are on the VFAT `modem` partition. All inspected partitions were block-device read-only, mounted `ro` without journal replay, and `userdata` was not mounted.

There are two different mailbox clients: SMP2P uses client 10 and the GLINK edge uses client 8. The existing modem path is separate: PID 1, client 12, and `IPCRTR`. Its constants and endpoint state must not be used for the ADSP.

## Startup flow

1. **Remain opt-in.** Ordinary boot performs no ADSP operation. `sweetadsp=1` records the opt-in and takes a read-only SMP2P snapshot. Only explicit `sweetadsp=2` reaches PAS startup.
2. **Validate firmware after root is available.** In `bsd_init()`, probe after `set_rootvnode(init_rootvnode)` and release of `rootvnode_rw_lock`. Mode 1 reads bounded ELF32 metadata. Mode 2 validates the complete split bundle, every `.bNN` sidecar and carveout bounds before PAS. Firmware remains outside Git.
3. **Check SMP2P lifecycle and prepare the AP-owned item.** Before mode-2 mutation, read remote-owned item 443 and refuse malformed headers or asserted `fatal`, `ready`, `handover`, or `stop-ack`. An absent item still means physical state is unknown and mode 2 is an explicit experiment. Initialize only AP-owned item 429 (local PID 0 to remote PID 2), using the Apps/ADSP SMEM private partition, TCSR mutex 3 and mailbox client 10. Do not write item 443. The SMP2P layout and two mailbox kicks follow Linux's allocator/protocol behavior; the current candidate also checks the mailbox mapping before editing shared memory.
4. **Attempt PAS ID 1 and observe Q6V5.** Require the firmware, ADT carveout marker, SCM command, SMP2P, AOSS QMP and RPMh XO checks first. `INIT_IMAGE`, `MEM_SETUP`, and `AUTH_AND_RESET` have now succeeded once on device. The current blocker is no `slave-kernel` ready indication after authentication. The driver's watchdog SPI 162 is not observed, so it does not issue `SHUTDOWN` after timeout and retains QMP/XO while remote state is unresolved.

The generic Linux PAS path enables proxy power domains and clocks before authentication. Beryllium's resource data has no named proxy domains, and the ADSP DTS node declares XO, AOSS QMP, SMP2P stop state, interrupts, and the carveout; no AGGRE2 clock or CX/PX supply is declared there. Keep the remaining boot-resource differences under investigation; do not treat the successful PAS return as proof that Q6 is ready.
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

The implementation belongs in `bsd/dev/beryllium_adsp.c`, registered through `bsd/conf/files`. Keep the early, read-only SMP2P snapshot beside the Beryllium bring-up call in `bsd/dev/sweet_ufs.c`; run the root-dependent firmware probe from `bsd/kern/bsd_init.c`, after `set_rootvnode()` and its lock release. Keep it isolated from `beryllium_wlan.c` and the MPSS driver's state. A small `beryllium_adsp_ipc.c`/header may own GLINK/APR if the next patch makes that split clearer.

The driver now owns bounded firmware probing, PAS, SMP2P inspection, AP-owned outbound-item setup and diagnostics. It does not access LPASS registers directly. [HakiOS's ADSP driver](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/xnu/files/bsd/dev/hakios.adsp.c) reports that its LPASS/I2S state is owned by the DSP; use the ADSP's AFE protocol once the APR control plane is understood. Its device-specific constants are not Beryllium values.

The early hook should log the read-only SMP2P snapshot; the post-root hook should log ELF metadata bounds, embedded versus expected image-wide split non-hash payloads, and the unclassified hash-metadata source. An expected `.bNN` sidecar is not evidence that it exists or has the required size. A later PAS/GLINK/APR boot can add those stage results after firmware, ownership, power, and transport details are observed. Do not add any analog route or PCM operation to this milestone.

## Evidence, inference, and unresolved points

**Observed:** Linux's Beryllium DTS and SDM845 topology define PAS ID 1, SMP2P items 443/429, GLINK items 478–480, mailbox clients 10/8 and APR audio services. Read-only TWRP extraction verified the split handset MDT and all nonempty segment sizes; firmware stays in ignored `.superpowers/firmware/beryllium/`. In CI run [41223692918](https://github.com/anxoCastro/Bergamota/actions/runs/41223692918), the `e412e20` kernel (22,230,464 bytes; SHA-256 `0aab71e09e2971f1863e361cd8472eccb81b4d9f1aca5a041e448c31bfdd3e17`) built successfully, with empty `errors.txt`. The temporary image `.superpowers/ci/41223692918/enxerto-adsp-pas-rootrw.img` was booted with Fastboot `product=beryllium`; `fastboot boot` returned OKAY and no partition was flashed. The captured log `.superpowers/ci/41223692918/live-udp-pas-rootrw.log` records a read-write HFS root, SpringBoard execution, no `vm_map_delete` panic, complete PAS command success (`status=0 result=0` for INIT_IMAGE, MEM_SETUP, AUTH_AND_RESET), then a five-second Q6V5 ready timeout with item 443 unavailable. This is the observed distinction between the earlier permanent-map panic and mount permissions. The current SMP2P outbound-item candidate has only local static checks so far and still needs a 26.5 CI build. The user reported SpringBoard on device after the run. Do not repeat a writable-root boot without fresh approval; the previous HFS inconsistency warning remains active. No partition was flashed.

**Inference:** the absence of inbound item 443 correlated with the Q6V5 timeout, but does not prove why the processor failed to publish ready. Adding AP-owned item 429, client-10 mailbox notification and the Linux q6v5 preparation step is the next experiment; a successful build will not establish that this is sufficient until a new device log is captured.

**Must be resolved before GLINK/APR and audio routing:**

- The Q6V5 ready/watchdog outcome and a read-only snapshot of current remote state after the latest timeout. Do not issue `SHUTDOWN` while the watchdog is unobserved.
- The precise remaining SDM845 PAS resource sequence for Beryllium, including any state not represented by its DTS and the effect of the prior PAS attempt.
- Whether the live Beryllium ADSP edge has Linux's SMEM items 478–480, what their actual sizes/indices and ownership are, and whether the edge is already active. Linux mainline's reference allocation and initialization are now traced, but not checked against the phone.
- Whether Beryllium exposes the Linux-configured `avs/audio` → `msm/adsp/audio_pd` protection domain and its runtime PDR state before Q6 core requests are sent.
- Whether `AVCS_GET_VERSIONS` is supported by the installed firmware and its response format/version.

Do not guess these values from HakiOS's Redwood port or from the modem driver. Record unknowns as runtime observations before adding later audio routes.

## Licensing boundary

Use Linux and HakiOS to identify protocol behavior, topology, and hardware facts. The local [LinuxKPI/licensing review](../LINUX_KCL.md) records the GPL boundary for Qualcomm audio. Implement the XNU driver independently; do not copy GPL ALSA/ASoC or remoteproc implementation code into this APSL tree. Retain provenance in documentation and credit upstream in `README.md` if code is later adapted, without co-author trailers.

## PAS bootstrap acceptance

- Ordinary boot performs no ADSP operation; mode 1 remains read-only.
- Mode 2 validates all firmware files and carveout bounds, refuses malformed or active SMP2P lifecycle state, and writes only outbound item 429.
- PAS calls are gated by SCM availability, the exact ADT marker, SMP2P preparation, QMP and XO prerequisites.
- The device reaches Q6V5 ready without a kernel panic before proceeding to GLINK/APR. The previous CI image passed PAS authentication but did not meet this condition.
- This milestone does not claim GLINK/APR connectivity or any audio route.
- Build and device evidence is recorded in `NOTES.md`; firmware and dumps stay outside Git.
