# Beryllium ADSP/APR Bootstrap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a safe, evidence-led path toward Poco F1 audio by first adding an opt-in, read-only ADSP preflight; defer PAS startup and GLINK/APR writes until the device firmware and runtime ownership are verified.

**Architecture:** Add an isolated Beryllium ADSP preflight that validates local firmware metadata and inspects the remote-owned SMP2P item without writing shared memory or touching PAS. Later stages may add PAS and a separate ADSP GLINK/APR endpoint for PID 2, mailbox client 8, and `apr_audio_svc`; leave the modem transport and all PCM/codec/HAL routes untouched.

**Tech Stack:** XNU 12377 kernel C, existing Beryllium boot-argument and firmware-loading patterns, Qualcomm SDM845 PAS/SMEM/SMP2P/GLINK/APR protocols, GitHub Actions kernel build.

**Spec:** `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`

## Global Constraints

- `sweetadsp=1` is required to run the ADSP preflight; default boot must not touch ADSP resources.
- Linux's DTS reports PAS ID 1 and Beryllium reserved memory `0x8c500000 + 0x01e00000`; this plan's first implementation must not assign or write that memory.
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

### Task 1: Add opt-in read-only ADSP preflight

**Files:**
- Create: `xnu/patches-12377/1131-beryllium-adsp-apr.patch`
- Create through the patch: `bsd/dev/beryllium_adsp.c`
- Create through the patch: `bsd/dev/beryllium_adsp.h`
- Modify through the patch: `bsd/conf/files`
- Modify through the patch: `bsd/dev/sweet_ufs.c`

**Interfaces:**
- Produces: `void beryllium_adsp_probe(void)`, called from the existing Beryllium bring-up point; the function returns immediately unless `sweetadsp=1`.
- Produces: `enum beryllium_adsp_state` with `OFF`, `PREFLIGHT`, `SMP2P_READY_OBSERVED`, `SMP2P_NOT_READY`, and `FAILED`; `bool beryllium_adsp_is_running_observed(void)` reports only the remote `ready` bit observation.
- Consumes: the existing root firmware path `/usr/standalone/firmware/sweet/` and the `sweet_ufs.c` initialization point, which runs after `rootvnode` is available.

- [x] Check the known local artifact paths for `adsp.mbn`; it is unavailable. Inspect the upstream PAS loader's ELF32/segment behavior and document that runtime validation remains outstanding. Implement only a bounded metadata parser that rejects unrecognized input.
- [x] Implement `beryllium_adsp_probe()` as an opt-in read-only preflight in `bsd/dev/beryllium_adsp.c`; validate firmware metadata, then report SMP2P observations without issuing PAS calls or writing shared memory.
- [x] Inspect only remote-owned SMP2P inbound item 443 for remote PID 2; validate the SMEM partition, item size, PIDs, entry count, and state name before reporting `ready`/`fatal` bits. Do not allocate or write outbound item 429.
- [x] Register the source in `bsd/conf/files` and call the probe from `sweet_ufs.c` beside the existing Beryllium bring-up hooks.
- [x] Record that Linux's PAS path enables proxy power domains and clocks before secure authentication; defer PAS until runtime ownership and XNU resource controls are confirmed.
- [x] Attempt `USE_KDK=1 tools/xnu-local-build.sh 26.5`; exit 2 before XNU compilation on Linux because BSD `sed -i ''` syntax is unavailable (`sed: can't read s/ARCHS="x86_64 arm64e" VALID_ARCHS="x86_64 arm64e"/ARCHS="x86_64 arm64e arm64" VALID_ARCHS="x86_64 arm64e arm64"/: No such file or directory`). No image produced.
- [x] Commit `cc866a9` as `feat: add read-only beryllium ADSP preflight`.

### Task 2: Add the ADSP GLINK endpoint

**Files:**
- Modify through the patch: `bsd/dev/beryllium_adsp.c`
- Create through the patch: `bsd/dev/beryllium_adsp_ipc.c`
- Modify through the patch: `bsd/dev/beryllium_adsp.h`
- Modify through the patch: `bsd/conf/files`

**Interfaces:**
- Consumes: a positive SMP2P ready observation from Task 1; that does not establish that PAS ownership or GLINK shared-memory state is safe to modify.
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
- Produces: `void beryllium_adsp_mark_apr_ready(void)` to publish the validated APR-ready transition to Task 1's state/logging.

- [ ] Implement APR header construction and sequence tracking for one outstanding core query.
- [ ] Parse only the expected APR response; bound the response payload and service-count fields before reading service/version entries.
- [ ] Log firmware build/version fields and unsupported-command status without treating unsupported firmware capability as a kernel fault.
- [ ] Run `USE_KDK=1 tools/xnu-local-build.sh 26.5`; record the exact result.
- [ ] Commit as `feat: query ADSP core versions over APR`.

### Task 4: Verify the opt-in preflight and record handoff

**Files:**
- Modify: `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`
- Modify: `NOTES.md`
- Build artifact: temporary kernel image only; do not flash a persistent boot partition.

**Interfaces:**
- Consumes: the Task 1 read-only preflight; PAS/GLINK/APR remain gated on firmware and device-state evidence.
- Produces: build/preflight evidence without flashing or changing partition contents.

- [ ] Run the GitHub Actions kernel build with `gh` once the implementation branch is available remotely; record the run ID and result.
- [ ] Run the read-only preflight on-device only after a compatible kernel build and authorized temporary boot are available; record exact arguments and `SWEET ADSP:` output.
- [ ] Confirm that a boot without `sweetadsp=1` has no ADSP preflight log and that no modem/WLAN regression appears in the observed boot.
- [ ] Update `NOTES.md` with the patch/commit, build/boot evidence, and remaining PAS/GLINK/APR prerequisites; leave firmware/dumps out of Git.
