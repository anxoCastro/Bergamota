# Beryllium ADSP/APR Bootstrap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in XNU bootstrap that starts the Poco F1 SDM845 ADSP and proves APR Q6 core service discovery.

**Architecture:** Add an isolated Beryllium ADSP worker for firmware validation, PAS lifecycle, and SMP2P readiness. Add a separate ADSP GLINK/APR endpoint for PID 2, mailbox client 8, and `apr_audio_svc`; leave the modem transport and all PCM/codec/HAL routes untouched.

**Tech Stack:** XNU 12377 kernel C, existing Beryllium boot-argument and firmware-loading patterns, Qualcomm SDM845 PAS/SMEM/SMP2P/GLINK/APR protocols, GitHub Actions kernel build.

**Spec:** `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`

## Global Constraints

- `sweetadsp=1` is required to run the ADSP worker; default boot must not touch ADSP resources.
- Use PAS ID 1 and Beryllium reserved memory `0x8c500000 + 0x01e00000`; validate runtime ownership before assignment.
- ADSP SMP2P uses SMEM items 443/429, local PID 0, remote PID 2, mailbox client 10.
- ADSP GLINK uses remote PID 2, mailbox client 8, and `apr_audio_svc`; determine and validate its SMEM transport items before use.
- Keep Qualcomm/Xiaomi firmware and device dumps outside Git.
- Do not reuse modem PID 1/client 12/`IPCRTR` state or GPU PAS ID 13/GPU memory constants.
- Stop at Q6 core APR version discovery; do not implement PCM, codec, amplifier, microphone, or iOS HAL routes in this plan.
- Do not copy GPL Linux audio/remoteproc implementation code into XNU.
- Preserve Android partitions and boot using temporary `fastboot boot` images; do not change GPT or flash a persistent boot image.

## Review Focus

- Missing, oversized, malformed, or incompatible firmware must fail before PAS or memory assignment.
- A DSP already running from the boot chain must not be reset or have memory reassigned blindly.
- SMP2P/GLINK timeout or malformed shared-memory state must terminate the probe without stalling iOS boot.
- APR replies with truncated payloads, oversized service counts, unsupported commands, or unexpected opcodes must be rejected safely.
- ADSP endpoint activity must not alter modem GLINK, MPSS, or existing WLAN state.

---

### Task 1: Add opt-in ADSP PAS lifecycle and SMP2P readiness

**Files:**
- Create: `xnu/patches-12377/1131-beryllium-adsp-apr.patch`
- Create through the patch: `bsd/dev/beryllium_adsp.c`
- Modify through the patch: `bsd/conf/files`
- Modify through the patch: `bsd/dev/sweet_ufs.c`

**Interfaces:**
- Produces: `void beryllium_adsp_probe(void)`, called from the existing Beryllium bring-up point; the function returns immediately unless `sweetadsp=1`.
- Produces: internal states `OFF`, `STARTING`, `DSP_READY`, `APR_READY`, and `FAILED`, with bounded waits and a single `SWEET ADSP:` log prefix.
- Consumes: the existing root firmware path `/usr/standalone/firmware/sweet/` and the `sweet_ufs.c` initialization point, which runs after `rootvnode` is available.

- [ ] Inspect the actual local `adsp.mbn` artifact before selecting its parser; record its format, size, and segment layout in `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md` without adding the blob to Git.
- [ ] Implement `beryllium_adsp_probe()` and the opt-in kernel worker in `bsd/dev/beryllium_adsp.c`; reject absent or invalid firmware before touching PAS or reserved memory.
- [ ] Implement PAS ID 1 startup/cleanup using only the Beryllium ADSP memory region and SDM845 resources confirmed from the device tree; detect and preserve an already-ready DSP.
- [ ] Implement bounded SMP2P readiness checks for items 443/429 and remote PID 2; log each state transition and stop on fatal/timeout.
- [ ] Register the source in `bsd/conf/files` and call the probe from `sweet_ufs.c` beside the existing Beryllium bring-up hooks.
- [ ] Run `USE_KDK=1 tools/xnu-local-build.sh 26.5`; record the exact result and keep the feature disabled in the default boot arguments.
- [ ] Commit as `feat: add opt-in beryllium ADSP bootstrap`.

### Task 2: Add the ADSP GLINK endpoint

**Files:**
- Modify through the patch: `bsd/dev/beryllium_adsp.c`
- Create through the patch: `bsd/dev/beryllium_adsp_ipc.c`
- Create through the patch: `bsd/dev/beryllium_adsp.h`
- Modify through the patch: `bsd/conf/files`

**Interfaces:**
- Consumes: Task 1's `DSP_READY` state.
- Produces: `bool beryllium_adsp_ipc_start(void)`, `bool beryllium_adsp_ipc_poll(uint64_t deadline_ms)`, and `void beryllium_adsp_ipc_stop(void)`.
- Endpoint parameters: remote PID 2, mailbox client 8, channel `apr_audio_svc`; SMEM descriptor/FIFO item numbers and ownership are resolved from SDM845 source/runtime evidence before they are encoded.

- [ ] Trace Linux's SDM845 GLINK SMEM edge initialization and the current Beryllium firmware state; document the exact ADSP descriptor/FIFO IDs and ownership before writing shared memory.
- [ ] Implement GLINK version negotiation, remote channel open, bounded RX/TX, and teardown in `beryllium_adsp_ipc.c`; keep its state independent from `beryllium_wlan.c`.
- [ ] Reject invalid descriptor sizes, FIFO offsets, ring indices, packet lengths, and unexpected channel IDs before copying or advancing shared state.
- [ ] Run `USE_KDK=1 tools/xnu-local-build.sh 26.5` and confirm the modem code is unchanged by the patch; record the exact result.
- [ ] Commit as `feat: add beryllium ADSP GLINK endpoint`.

### Task 3: Discover the APR Q6 core service

**Files:**
- Modify through the patch: `bsd/dev/beryllium_adsp_ipc.c`
- Modify through the patch: `bsd/dev/beryllium_adsp.h`

**Interfaces:**
- Consumes: `beryllium_adsp_ipc_start()`, `beryllium_adsp_ipc_poll()`, and the open `apr_audio_svc` channel.
- Produces: `bool beryllium_adsp_apr_query_core_versions(uint32_t timeout_ms)`; query service 3 with `AVCS_GET_VERSIONS` opcode `0x00012905`, validate its response, and transition to `APR_READY` only on a valid reply.

- [ ] Implement APR header construction and sequence tracking for one outstanding core query.
- [ ] Parse only the expected APR response; bound the response payload and service-count fields before reading service/version entries.
- [ ] Log firmware build/version fields and unsupported-command status without treating unsupported firmware capability as a kernel fault.
- [ ] Run `USE_KDK=1 tools/xnu-local-build.sh 26.5`; record the exact result.
- [ ] Commit as `feat: query ADSP core versions over APR`.

### Task 4: Verify the opt-in probe on device and record handoff

**Files:**
- Modify: `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`
- Modify: `NOTES.md`
- Build artifact: temporary kernel image only; do not flash a persistent boot partition.

**Interfaces:**
- Consumes: the complete PAS → SMP2P → GLINK → APR implementation from Tasks 1–3.
- Produces: one CI build ID and one exact temporary boot result with the complete boot arguments and ADSP log sequence.

- [ ] Run the GitHub Actions kernel build with `gh`; record the run ID and result.
- [ ] Boot the image with `fastboot boot` and `sweetadsp=1`; verify iOS reaches its normal boot target and collect the `SWEET ADSP:` log sequence.
- [ ] Confirm that a boot without `sweetadsp=1` has no ADSP startup log and that no modem/WLAN regression appears in the observed boot.
- [ ] Update `NOTES.md` with the patch/commit, CI run ID, exact build/boot results, and unresolved work; leave firmware/dumps out of Git.
- [ ] Commit the documentation and handoff update.
