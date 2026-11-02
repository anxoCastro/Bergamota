# Beryllium ADSP GLINK Read-Only Preflight Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded, read-only survey of ADSP GLINK-SMEM items 478–480 after Q6V5 ready, without starting GLINK/APR or changing SMEM.

**Architecture:** Add pure snapshot validation helpers in the new ordered XNU patch 1148 and compile those exact helpers in a host C harness. Extend the existing ADSP driver with a read-only Apps/ADSP item lookup that distinguishes found, absent, and malformed; take two double-read descriptor samples 100 ms apart after Q6V5 ready. Python patch-contract checks will verify call ordering and prove that the survey does not reach allocators, SMEM writers, mailbox 8, IRQ 156, GLINK, or APR.

**Tech Stack:** XNU 26.5 C patch series, C11 host regression harness, Python unittest patch-contract checks, GitHub Actions `xnu.yml`, Fastboot RAM boot, UDP/5140 capture.

**Spec:** [2026-11-02-adsp-glink-readonly-preflight-design.md](../specs/2026-11-02-adsp-glink-readonly-preflight-design.md)

## Global Constraints

- Keep survey opt-in under the existing `sweetadsp=2` mode; ordinary boot and mode 1 remain unchanged.
- Inspect items 478, 479, and 480 in both cached and uncached lists of the Apps/ADSP private SMEM partition for hosts 0 and 2. Report `ABSENT` only after both valid lists are scanned.
- Item 478 is interpreted only when its reported size is exactly 32 bytes; items 479 and 480 are reported as found/absent with their actual sizes.
- Record descriptor word 0 TX tail, word 1 TX head, word 2 RX tail, and word 3 RX head.
- Take two descriptor/index samples 100 ms apart; read words 0–3 twice within each sample and mark a sample unstable if the reads differ.
- Do not allocate SMEM, modify any descriptor/FIFO byte or index, ring mailbox 8, enable GIC IRQ 156, negotiate GLINK, open APR, or perform audio operations.
- Keep mode 2's PAS sequence, SMP2P item 443 read-only behavior, QMP/XO policy, and timeout behavior unchanged.
- Hardware verification uses a temporary `fastboot boot` root-read-only image only; do not flash partitions or change GPT.
- Keep device dumps, serials, and firmware outside Git. Add no co-author trailers.

## Review Focus

- Missing item 478: report absent and do not read descriptor words or allocate a replacement; pin in the parser/driver integration check.
- Malformed private partition/item metadata, including a header size that differs from the TOC size: classify malformed separately from absent and stop the survey without panic; pin in the compiled parser fixture.
- Descriptor size other than 32 bytes: log the size without interpreting its contents; pin in the compiled C helper test and patch-contract check.
- Missing FIFO 479 or 480: report its size as unknown and avoid range-checking the associated ring; pin in the compiled C helper test.
- TX/RX index equal to or greater than its FIFO size: report out of range and do not repair it; pin in the compiled C helper test.
- The four indices differ within a sample or between 100 ms samples: classify as unstable/changed and make no writes; pin in the compiled C helper test.
- Q6V5 ready is absent: do not run the survey; pin in the driver call-order check.
- Survey call graph reaches an allocator, SMEM writer, mailbox-8 doorbell, IRQ-156 setup, GLINK parser, or APR function: fail the static patch-contract check.

---

### Task 1: Add pure GLINK snapshot helpers and compiled regression tests

**Files:**
- Add through patch 1148: `bsd/dev/beryllium_adsp_glink_survey.h`
- Create: `tools/tests/test_adsp_glink_survey.c`
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Create: `xnu/patches-12377/1148-beryllium-adsp-glink-readonly-preflight.patch`

**Interfaces:**
- `enum beryllium_adsp_item_result`: `FOUND`, `ABSENT`, `MALFORMED`.
- `struct beryllium_adsp_glink_indices`: `uint32_t tx_tail`, `tx_head`, `rx_tail`, `rx_head`.
- `bool beryllium_adsp_glink_descriptor_size_valid(uint32_t size)` returns true only for 32 bytes.
- `enum beryllium_adsp_glink_index_result beryllium_adsp_glink_check_index(uint32_t value, bool fifo_present, uint32_t fifo_size)` returns `VALID`, `UNCHECKED_FIFO_MISSING`, or `OUT_OF_RANGE`.
- `bool beryllium_adsp_glink_indices_equal(const struct beryllium_adsp_glink_indices *a, const struct beryllium_adsp_glink_indices *b)` compares all four words.
- All helpers are side-effect free.

- [x] **Step 1: Write the failing C harness and unittest entry.** Add assertions for descriptor sizes 0, 16, 31, 32, and 33; absent TX/RX FIFOs; indices 0 and `size - 1`; indices equal to size and greater than size; and equal/different index sets. Add `test_compiled_glink_survey_helpers` to `tools/tests/test_adsp_pas_patch.py`, extracting the header from patch 1148 and compiling it with the harness.
- [x] **Step 2: Run the harness integration test and confirm failure.** Run `python3 tools/tests/test_adsp_pas_patch.py AdspPasPatchTests.test_compiled_glink_survey_helpers`; expected: failure because patch 1148 and its helper header do not yet exist.
- [x] **Step 3: Add the pure helper header in patch 1148.** Keep it independent of XNU headers so the host compiler can compile the exact production helper.
- [x] **Step 4: Run the focused test.** Run `python3 tools/tests/test_adsp_pas_patch.py AdspPasPatchTests.test_compiled_glink_survey_helpers`; expected: the exact header from patch 1148 compiles with `cc -std=c11 -Wall -Wextra -Werror` and all descriptor, missing-FIFO, bounds, and comparison cases pass.
- [x] **Step 5: Commit the helper and host regression.** Use a message that names the read-only GLINK survey; add no co-author trailer.

### Task 2: Classify bounded private-SMEM lookup results

**Files:**
- Modify through patch 1148: `bsd/dev/beryllium_adsp.c`
- Modify: `tools/tests/test_adsp_pas_patch.py`

**Interfaces:**
- Change `adsp_smem_private_item(uint16_t host_a, uint16_t host_b, uint16_t wanted, uint32_t *data_off, uint32_t *data_size)` to return `enum beryllium_adsp_item_result`.
- Return `FOUND` only after the bounds/canary/range checks pass for both lists; return `ABSENT` only when neither valid list contains the requested item; return `MALFORMED` for invalid TOC, unequal partition sizes, invalid free offsets, item header, canary, cacheline, or payload bounds.
- Update existing call sites to compare explicitly with `FOUND`, preserving their existing behavior for absent or malformed items.

- [x] **Step 1: Add patch-contract cases for lookup classification.** Assert the enum returns and ensure the function does not call `adsp_smem_wr*`, the allocator, or the TCSR lock.
- [x] **Step 2: Run the focused Python test and confirm failure.** Run `python3 tools/tests/test_adsp_pas_patch.py AdspPasPatchTests.test_smem_lookup_classifies_absent_and_malformed`; expected: failure because lookup still returns `bool`.
- [x] **Step 3: Implement the result enum and update call sites.** Preserve every existing structural validation before returning `FOUND`.
- [x] **Step 4: Run the focused Python test.** Expected: absent and malformed states have distinct branches and existing mode-2 gates remain in place.
- [x] **Step 5: Commit patch 1148 lookup changes.**

### Task 3: Implement the post-ready read-only survey

**Files:**
- Modify through patch 1148: `bsd/dev/beryllium_adsp.c`
- Modify: `tools/tests/test_adsp_pas_patch.py`

**Interfaces:**
- Add `static void adsp_glink_readonly_survey(void)`.
- Call it once from `beryllium_adsp_pas_start()` only after the existing Q6V5-ready log/branch succeeds.
- Survey items 478/479/480 only in the host-0/host-2 private partition.

- [x] **Step 1: Add failing patch-contract checks.** Assert the survey call follows `Q6V5 ready observed`, item 478 requires size 32, item status is handled as found/absent/malformed, FIFO sizes are reported, and no write/allocation/mailbox/IRQ/GLINK/APR symbols occur in the survey function body.
- [x] **Step 2: Run the focused test and confirm failure.** Run `python3 tools/tests/test_adsp_pas_patch.py AdspPasPatchTests.test_glink_survey_is_read_only_and_after_ready`; expected: failure because the survey function does not exist.
- [x] **Step 3: Implement sample 1.** Read item sizes and descriptor words 0–3 with the current SMEM read path and barriers. Read the four words twice; mark sample 1 unstable if the two sets differ. Do not print physical offsets.
- [x] **Step 4: Implement sample 2.** Wait 100 ms, repeat the same bounded reads, and classify unchanged values as `stable during 100 ms`, changed values as `changed between samples`, or invalid/unstable data without interpreting it.
- [x] **Step 5: Log the `slave-kernel` name and value.** Add the direct read-only entry log at the successful Q6V5-ready point; do not change item 443.
- [x] **Step 6: Run the focused contract test.** Expected: survey is reachable only after ready, and its call graph contains no shared-memory mutation or GLINK/APR start.
- [x] **Step 7: Commit the survey implementation and safety checks.**

### Task 4: Verify the ordered patch series and regression suite

**Files:**
- Modify: `tools/tests/test_adsp_pas_patch.py`
- Add: `tools/tests/adsp_smem_private_item_harness.c`
- Modify through patch 1148: `bsd/dev/beryllium_adsp.c`
- Add: `xnu/patches-12377/1148-beryllium-adsp-glink-readonly-preflight.patch`

- [x] **Step 1: Extend ordered patch application through 1148.** Assert patches 1131–1148 apply in order and the latest driver contains the survey.
- [x] **Step 2: Run ADSP patch regression tests.** Run `python3 tools/tests/test_adsp_pas_patch.py`; expected: all C harnesses and integration checks pass, including the extracted production parser against cached/uncached and malformed fixtures.
- [x] **Step 3: Run repository test discovery.** Run `python3 -m unittest discover -s tools/tests`; expected: all existing and new tests pass.
- [x] **Step 4: Run `git diff --check`.** Expected: clean outside unified patch context whitespace conventions.
- [x] **Step 5: Commit the regression and patch-order updates.**

### Task 5: Build and capture the read-only hardware survey

**Files:**
- Build: `.github/workflows/xnu.yml`
- Update: `NOTES.md`
- Artifacts: ignored `.superpowers/ci/<run-id>/`

- [x] **Step 1: Push the reviewed parser fix and dispatch KDK-enabled `xnu.yml`** on `audio-poco-f1-adsp-bootstrap` for macOS 26.5, Xcode 26.1, DEVELOPMENT-SWEET, and keep-going enabled. The obsolete run 41482463386 built no steps and ended cancelled while queued on the pre-fix SHA. Replacement run 41486126502 on `727eedd7942378453529830013fb81e905da1306` also ended cancelled after 15 minutes with `runner_name=null` and no steps; neither run produced an artifact.
- [x] **Step 1b: Retry the same KDK build with `runner_label=macos-26-intel`.** The original `macos-26` pool did not assign a runner in 15 minutes. Run 41489245547 is executing on `macos-26-intel`, pinned to workflow-override commit `93aab9345182fa32ac3e4c920c0218590bf882b3`; setup, checkout and build-script fetch passed, with dependencies installing. Kernel patch source remains identical to code commit `727eedd`.
- [ ] **Step 2: Use `gh` to inspect the run and artifacts.** Record run ID, pinned SHA, patch-1148 application, empty compiler-error report, kernel hash, and linker warnings.
- [ ] **Step 3: Package a temporary root-read-only Enxerto image.** Keep `sweetadsp=2`, omit `ufsrw=1`/`rootrw=1`, verify `/chosen` marker and image hash.
- [ ] **Step 4: Verify Fastboot identity and boot the image temporarily.** Require `product=beryllium` and `unlocked=yes`; start UDP/5140 capture before `fastboot boot`; do not flash any partition.
- [ ] **Step 5: Inspect and hash the capture.** Expect the repeated PAS/SMP2P/Q6V5-ready result plus item 478–480 sizes and two sample classifications. Confirm the capture contains no GLINK/APR command and distinguish the later `mount` result from ADSP output.
- [ ] **Step 6: Update `NOTES.md`, run `git diff --check`, commit, and push.** Keep the capture and image under ignored `.superpowers/`.
