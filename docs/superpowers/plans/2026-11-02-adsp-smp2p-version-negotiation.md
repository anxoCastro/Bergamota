# ADSP SMP2P Version Negotiation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Make XNU negotiate AP-owned SMP2P item 429 down to the valid remote item 443 version, then verify the handshake in a read-only Poco F1 boot.

**Architecture:** Add a bounded negotiation helper to the existing Beryllium ADSP driver. It reads the remote header, updates only the AP-owned item when Linux would downgrade its version, intersects features after versions match, and uses the existing mailbox-10 kick. Keep PAS, item 443, and audio startup unchanged.

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

### Task 1: Add a regression contract for Linux-compatible negotiation

**Files:**
- Modify: tools/tests/test_adsp_pas_patch.py
- Test: tools/tests/test_adsp_pas_patch.py

**Interfaces:**
- Consumes: the ordered 1131–1146 patch series.
- Produces: a latest_driver fixture that applies patch 1147 and a test for adsp_smp2p_negotiate_remote().

- [ ] **Step 1: Write the failing regression test.** Extend the patch-order bound to 1147. Assert the helper reads a validated nonzero remote version, accesses only ADSP_SMP2P_OUT_ITEM, lowers only when remote version is below local, preserves feature bits, kicks after a downgrade, intersects remote features only after versions match, refuses upgrades, and is called from the existing ready loop after the read-only inbound probe.
- [ ] **Step 2: Run the test and verify it fails.** Run python3 tools/tests/test_adsp_pas_patch.py; expected: the latest-driver test fails because patch 1147 and adsp_smp2p_negotiate_remote() do not exist.
- [ ] **Step 3: Implement the minimum ordered patch.** Add xnu/patches-12377/1147-beryllium-adsp-smp2p-version-negotiation.patch. Store the AP item-429 offset when it is allocated. Add adsp_smp2p_negotiate_remote() that validates both headers, follows Linux's downgrade and feature-intersection rules, writes no bytes from item 443, and kicks only after a version downgrade. Call it during each existing post-PAS poll after adsp_smp2p_readonly_probe(true).
- [ ] **Step 4: Run the regression suite.** Run python3 tools/tests/test_adsp_pas_patch.py; expected: all tests pass and patches 1131–1147 apply in order.
- [ ] **Step 5: Commit the patch and regression check.** Commit with a message describing SMP2P version negotiation; add no co-author trailer.

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
