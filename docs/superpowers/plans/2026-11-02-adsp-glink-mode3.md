# Beryllium ADSP GLINK Mode 3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task.

**Goal:** Add explicit `sweetadsp=3` cold-start validation and a bounded GLINK v1 exchange after Q6V5 ready, while preserving the read-only `sweetadsp=2` path.

**Architecture:** Extend the ordered patch series with 1149. Implement mode selection, AP-owned FIFO preparation under TCSR mutex 3, stable-zero descriptor validation, one GLINK VERSION request and bounded polling. Test pure packet/ring logic by compiling the exact helper header from the patch, and assert mutation ownership and call ordering from the patch contract.

**Spec:** [2026-11-02-adsp-glink-mode3-design.md](../specs/2026-11-02-adsp-glink-mode3-design.md)

## Global constraints

- Use the existing isolated `audio-poco-f1-adsp-bootstrap` worktree; do not create another worktree.
- Preserve default boot, modes 1 and 2, and the modem GLINK implementation.
- Mode 3 explicitly requests the same PAS startup as mode 2, then a GLINK-only probe after Q6V5 ready.
- Allocate only private Apps/ADSP item 479, only if absent; never allocate item 480.
- Hold TCSR mutex 3 while revalidating and changing the private allocation list.
- Never reset descriptor state. Before first TX, require descriptor and both FIFOs valid and all four ring indices stable, aligned, and zero.
- Write only AP-owned SMEM: new item-479 metadata/payload, TX FIFO command bytes, TX head, and RX tail when consuming a recognized version packet. Never write TX tail, RX head, item 480, inbound item 443, or client 10.
- Send one 8-byte VERSION v1 with features 0, ring only mailbox client 8, and poll for at most 2 seconds. Stop before APR/audio.
- Keep the test image root read-only; only use `fastboot boot`. Do not flash partitions or change GPT.
- Add no co-author trailer. Keep serials/dumps/firmware outside Git.

## Task 1: Pin protocol rules in helper tests (TDD)

**Files:**
- Modify through patch 1149: `bsd/dev/beryllium_adsp_glink_survey.h` or a small companion helper header
- Create `tools/tests/test_adsp_glink_mode3.c`
- Modify `tools/tests/test_adsp_pas_patch.py`

- [x] Add failing compiled cases for zero-only cold-start state, nonzero/unaligned/out-of-range indices, TX/RX FIFO size limits, ring wrap, 8-byte VERSION and VERSION_ACK decoding, wrong packet length, unknown GLINK command, and `sweetadsp` modes 0–3.
- [x] Add static contract assertions proving mode 2 still invokes only the survey, mode 3 is the only new start path, it follows Q6V5 ready, mode 3 cannot call APR, and all writes target AP-owned locations/mailbox client 8.
- [x] Run the focused tests and confirm they fail because patch 1149/driver changes are not present.

## Task 2: Add safe item-479 allocation and mode-3 gate

**Files:**
- Create `xnu/patches-12377/1149-beryllium-adsp-glink-mode3.patch`

- [x] Stage the exact driver by applying patches 1131–1148 in order; implement changes in that derived source.
- [x] Add mode 3 while preserving mode-2 behavior; mode 3 continues only through the existing PAS/Q6V5-ready path.
- [x] Under hwspinlock 3, revalidate the unique Apps/ADSP partition and scan both item lists. Accept an existing 479 only at 16 KiB; allocate only if absent and enough uncached space remains before the cached boundary. Publish the list head after a barrier.
- [x] After allocation, re-read items 478–480 and require valid descriptor size and FIFO sizes. Double-read all indices and wait 100 ms; require stable all-zero state before progressing. Do not write a reset value.
- [x] Add bounded GLINK ring routines using SMEM byte/word access with wrap and alignment checks; TX the 8-byte v1/features-0 packet, publish TX head only after a barrier, then ring bit 8.
- [x] Poll at most 2 seconds. Accept/consume only complete VERSION or VERSION_ACK packets with supported version and features 0; send a base ACK for remote VERSION. Consume only by updating AP-owned RX tail. Leave unknown traffic untouched and abort.
- [x] Confirm no setup path changes TCSR/SMP2P/PAS sequencing or remote-owned SMEM.

## Task 3: Pass tests, patch application, and review

- [x] Add production helper(s) to the exact patch and make the compiled C harness pass with warnings as errors.
- [x] Run focused ADSP patch tests and full `tools/tests` suite after the requested implementation.
- [x] Apply the ordered patch series through 1149 and inspect the resulting driver call graph/diff for ownership boundaries.
- [x] Run `git diff --check` on source/doc changes; exclude unified patch artifacts if their context lines trigger false whitespace checks.
- [ ] Update this plan checkboxes and write the implementation/build/boot status into `NOTES.md`.

## Task 4: Build and RAM-only device experiment

- [ ] Trigger the GitHub Actions XNU build with `gh`, pinned to this branch head; inspect job logs and artifacts. Require empty `errors.txt`, verified SHA256SUMS, and patch 1149 in apply output.
- [ ] Package the KDK-built kernel using the already verified Enxerto loader and Beryllium ADT, with `sweetadsp=3`, `rd=ufsroot0`, and no `ufsrw=1`/`rootrw=1`.
- [ ] Unpack and verify kernel/payload/ADT marker/boot header and hashes before hardware use.
- [ ] Recheck exactly one Fastboot target, `product=beryllium`, and unlocked status; capture UDP/5140 before `fastboot boot`.
- [ ] RAM-boot only. Record the exact send/boot result, relevant PAS/Q6V5/GLINK output, log hash, and whether boot continued. Stop if the phone leaves Fastboot or the preflight differs; do not flash any partition.

## Task 5: Handoff and commit

- [ ] Update `NOTES.md` with commit, CI run ID, kernel/image hashes, exact Fastboot result and GLINK response/timeout, and remaining work; clearly distinguish observations from interpretation.
- [ ] Commit the completed source/tests/docs/handoff without any co-author trailer and push the active branch.

## Completion report

- Summarize the behavior, test/build results, exact RAM-boot observation, and unresolved APR/audio gates with links to local files and CI run.
