# Beryllium ADSP SMP2P entry name Implementation Plan

> **For agentic workers:** Implement this plan natively under the user's standing instruction to continue autonomously.

**Goal:** Make AP-owned ADSP SMP2P item 429 publish the wire name `master-kernel`, while preserving bit 0 as the clear `stop` state.

**Architecture:** Add patch 1140 after the existing ADSP patches. It changes only the 16-byte outbound entry name and its diagnostic label. Update the ADSP design and handoff, validate the patch stack and build with the KDK-enabled GitHub Actions workflow, then use a temporary Fastboot boot only when the host sees an unlocked `beryllium` target.

**Tech Stack:** XNU 12377 C patch series, GitHub Actions, Android Fastboot.

**Spec:** `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md` and upstream SDM845 Linux SMP2P/PAS definitions.

## Global Constraints

- Do not add or run tests unless the user asks.
- Do not modify remote-owned item 443, issue PAS `SHUTDOWN`, or start GLINK/APR while no valid `slave-kernel` entry exists.
- Preserve Android and all partitions; device validation uses `fastboot boot` only.
- Keep causal claims provisional until the corrected image is observed on the handset.

## Review Focus

- The outbound 16-byte name must be exactly `master-kernel`, NUL-terminated and zero-padded.
- Its 32-bit value must remain clear so Q6V5 sees `stop=0`.
- Item 429 ownership, header, valid-entry count, doorbell and PAS sequence must remain unchanged.
- CI must apply the full ordered patch stack and complete the XNU build.
- A missing Fastboot target means no device boot was performed.

### Task 1: Correct the AP-owned ADSP entry name

**Files:**
- Create: `xnu/patches-12377/1140-beryllium-adsp-smp2p-master-kernel.patch`
- Modify: `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`
- Modify: `NOTES.md`

- [x] Encode `master-kernel` in item 429 and label the clear bit as `stop bit 0` in the log.
- [x] Validate the revised hunks with `git apply --check` and apply them to the saved post-1139 XNU source; only the outbound name and log label change.
- [ ] Dispatch the KDK-enabled XNU 26.5 GitHub Actions build and record exact run ID, kernel hash and result.
- [ ] If Fastboot enumerates `product=beryllium`, boot the generated image temporarily; never flash a partition or GPT.
- [ ] Record boot evidence or the absent-device result, update the handoff, and commit/push without co-author trailers.
