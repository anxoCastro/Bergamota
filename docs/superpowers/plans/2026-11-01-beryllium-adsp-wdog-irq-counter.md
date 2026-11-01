# Beryllium ADSP watchdog IRQ counter implementation plan

> **For Codex:** implement inline in the existing audio worktree. Do not spawn agents. Follow the design in `docs/superpowers/specs/2026-11-01-beryllium-adsp-wdog-irq-counter-design.md`. Use patch-application checks, CI build/log review, and hardware observation.

**Goal:** determine whether the Beryllium ADSP asserts watchdog GIC SPI 162 during PAS startup without changing interrupt acknowledgement or remote state.

**Architecture:** add a relaxed atomic counter at the existing SWEET `sleh_irq()` dispatch point, expose a count getter, snapshot it around `AUTH_AND_RESET`, and log the delta with the existing timeout/fatal diagnostics. Keep this in patch 1134 after PAS patches 1132 and 1133.

**Files:**

- `xnu/patches-12377/1134-beryllium-adsp-wdog-irq-counter.patch` — SLEH counter/getter and ADSP count-delta reporting.
- `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md` — source audit, current boot state, and result.
- `NOTES.md` — current handoff, CI/build/image hashes, exact device outcome, remaining work.

## Task 1: Implement the passive counter

1. Create patch 1134 against the already-patched XNU buildtree context. Count only `intid == 162` with XNU's atomic primitives; do not log, mask, acknowledge, or short-circuit in `sleh_irq()`.
2. Export a getter returning the atomic count. In the ADSP driver, capture the count immediately before `AUTH_AND_RESET`; report the modulo-32-bit delta after a watchdog/fatal timeout snapshot.
3. Rename the patch 1132 failure diagnostic from “q6v5_prepare equivalent” to “outbound SMP2P preparation” so it names the operation the helper actually performs.
4. Run `git diff --check` and `git apply --numstat` on patch 1134.

## Task 2: Build and package

1. Commit the source/docs milestone without a co-author trailer and push the branch.
2. Dispatch the repository XNU kernel workflow with `gh` for XNU 26.5; record the run ID and inspect the complete patch-application/build log plus `errors.txt`.
3. Package the kernel with the verified Enxerto loader/ADT and `ufsrw=1 rootrw=1 sweetadsp=2`; record hashes and verify the ADT marker and image contents before device boot.

## Task 3: Temporary Beryllium boot

1. Confirm `fastboot getvar product` returns `beryllium` and use only `fastboot boot` for the candidate.
2. Isolate the new UDP log from the active cumulative stream and record exact lines, byte count, SHA-256, PAS status, watchdog counter delta, mount result, and SpringBoard process result.
3. Make no partition/GPT changes and do not issue PAS `SHUTDOWN` or start GLINK/APR while item 443 has no `slave-kernel` entry.

## Task 4: Handoff

Update `NOTES.md` and the ADSP design doc with observations versus inference, the CI run and hashes, exact boot result, and the next concrete investigation. Commit and push the completed milestone.
