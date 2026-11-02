# ADSP SMP2P Version Negotiation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Make XNU negotiate AP-owned SMP2P item 429 down to the valid remote item 443 version, then verify the handshake in a read-only Poco F1 boot.

**Architecture:** Put the protocol transition in a small production C header included by the Beryllium ADSP driver. A host harness compiles and executes that exact helper with literal SMP2P headers; Python patch-integration checks cover AP-item ownership and the mailbox call site. Keep PAS, item 443, and audio startup unchanged.

**Tech Stack:** XNU 26.5 C patch series, Python unittest patch-contract checks, GitHub Actions workflow xnu.yml, Fastboot RAM boot, UDP/5140 kernel capture.

**Spec:** docs/superpowers/specs/2026-11-02-adsp-smp2p-version-negotiation-design.md

## Global Constraints

- Keep remote-owned item 443 read-only.
- Do not change PAS arguments, QMP/XO policy, timeout, or failure cleanup.
- Do not issue PAS SHUTDOWN or start GLINK/APR/audio before item 443 contains a valid slave-kernel entry.
- Boot only with temporary fastboot boot images; do not flash a partition or change GPT.
- Use a root-read-only image for the next hardware test; preserve the existing userdata backup.
- Keep device dumps and serial numbers out of Git.
- Add no co-author trailer; commit completed changes and update NOTES.md.

## Review Focus

- Missing remote item 443: item 429 stays at v2 and no negotiation kick occurs; pin in the regression test.
- Valid remote v1 with local v2: item 429 lowers to v1, preserves the feature byte, and kicks once; pin in the regression test.
- Equal versions: local features are intersected with remote features and negotiation completes without an unnecessary kick; pin in the regression test.
- Remote version higher than local or malformed header: do not upgrade or mutate item 429; pin the valid-version gates in the regression test.
- Missing or malformed AP item 429: refuse to write and do not touch item 443; pin the ownership and bounds checks in the regression test.

---

### Task 1: Add a compiled regression contract and Linux-compatible negotiation patch

**Files:**
- Modify: tools/tests/test_adsp_pas_patch.py
- Create: tools/tests/test_adsp_smp2p_negotiation.c
- Add through patch 1147: bsd/dev/beryllium_smp2p_negotiation.h
- Add: xnu/patches-12377/1147-beryllium-adsp-smp2p-version-negotiation.patch

**Interfaces:**
- Consumes: the ordered 1131–1146 patch series.
- Produces: beryllium_smp2p_negotiate_header(uint32_t *local_head, uint32_t remote_head), a latest_driver fixture including patch 1147, and an integration path from the post-PAS poll.

- [ ] **Step 1: Write the failing executable regression.** Add a C harness with hand-derived header values for: local v2/feature 1 plus remote v1/feature 1 -> DOWNGRADE and local v1/feature 1; equal v1 with remote feature 0 -> FEATURES_CHANGED and local feature 0; equal v1/features -> COMPLETE unchanged; remote v2 with local v1 -> WAIT unchanged; and zero/unsupported versions -> INVALID unchanged. Extend the Python patch test to extract the new header from patch 1147, compile it with cc, and run the harness. Add integration assertions that the driver calls negotiation after its read-only item-443 probe, writes only ADSP_SMP2P_OUT_ITEM, kicks on DOWNGRADE, and never stores to item 443. Extend the patch-order bound to 1147.
- [ ] **Step 2: Run the test and verify it fails for the missing feature.** Run python3 tools/tests/test_adsp_pas_patch.py AdspPasPatchTests.test_smp2p_version_negotiation_is_wired_to_ap_item; expected: this one integration test fails because adsp_smp2p_negotiate_remote() does not yet exist.
- [ ] **Step 3: Implement the minimum ordered patch.** Add patch 1147 with the pure header transition helper and the driver integration. The driver stores the AP item-429 offset when publishing it, validates its header before updating, preserves features while lowering the version, retries a pending mailbox kick if necessary, intersects features only once versions match, and never writes item 443. Call the driver helper after each read-only inbound probe in the existing bounded poll.
- [ ] **Step 4: Run the regression suite.** Run python3 tools/tests/test_adsp_pas_patch.py; expected: the host C harness passes and patches 1131–1147 apply in order.
- [ ] **Step 5: Commit the patch and regression checks.** Commit with a message describing SMP2P version negotiation; add no co-author trailer.

### Task 2: Update protocol documentation and handoff

**Files:**
- Modify: docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md
- Modify: NOTES.md

**Interfaces:**
- Consumes: patch 1147 and its regression-test results.
- Produces: an accurate statement of the v2/v1 mismatch, negotiation behavior, and remaining hypothesis.

- [ ] **Step 1: Correct the Linux comparison.** Document that Linux can start with v2 when the remote item is absent, then downgrade and kick after receiving a lower-version remote header. Record that XNU previously lacked this response.
- [ ] **Step 2: Keep the cause labeled as a hypothesis.** State that the observed v2/v1 mismatch correlates with the empty item 443 but does not prove the cause of the SFR report.
- [ ] **Step 3: Verify the documentation diff.** Run git diff --check; expected: clean.
- [ ] **Step 4: Commit documentation and update NOTES.md** with the test/build/boot results available at this point.

### Task 3: Build and package a read-only diagnostic image

**Files:**
- Build: .github/workflows/xnu.yml
- Artifacts: ignored .superpowers/ci/<run-id>/

**Interfaces:**
- Consumes: committed patch 1147 and passing local regression suite.
- Produces: an XNU 26.5 DEVELOPMENT-SWEET kernel and a temporary root-read-only Beryllium boot image.

- [ ] **Step 1: Dispatch the fork workflow.** Run the xnu.yml workflow on anxoCastro/Bergamota branch audio-poco-f1-adsp-bootstrap with macOS 26.5, Xcode 26.1, DEVELOPMENT, SWEET, KDK enabled, and keep_going enabled.
- [ ] **Step 2: Verify build output.** Use gh to wait for completion and inspect logs/artifacts. Record run ID, pinned commit, kernel SHA-256, empty compiler-error report, and linker warnings/errors.
- [ ] **Step 3: Package the image.** Reuse the known-good Enxerto loader and Beryllium ADT, set sweetadsp=2 and the verified ADSP region marker, and omit ufsrw=1/rootrw=1. Verify the packaged payload and boot-image header.
- [ ] **Step 4: Commit the build/image handoff.** Record the run and prepared image hashes in NOTES.md; keep all binary artifacts ignored.

### Task 4: Verify the negotiation on the Poco F1

**Files:**
- Device: Poco F1, Fastboot RAM boot only
- Capture: ignored .superpowers/device-backups/

**Interfaces:**
- Consumes: the root-read-only image from Task 3 and the already staged firmware bundle.
- Produces: a complete UDP capture beginning before boot and a factual pass/fail result.

- [ ] **Step 1: Confirm Fastboot identity.** Require product beryllium and unlocked yes. Do not use fastboot flash.
- [ ] **Step 2: Start UDP/5140 capture before boot.** Write the raw stream to an ignored timestamped file.
- [ ] **Step 3: Run fastboot boot only.** Record send/boot result and elapsed time; make no partition or GPT changes.
- [ ] **Step 4: Inspect negotiation evidence.** Expect a single item-429 v2-to-v1 update and mailbox-10 kick after item 443 v1 appears, followed by remote slave-kernel publication. If it remains empty, record the result without issuing SHUTDOWN or starting GLINK/APR.
- [ ] **Step 5: Close the capture, hash it, update NOTES.md, and commit the handoff.** Record whether any later read-only-root mount panic occurred separately from the early ADSP result.
