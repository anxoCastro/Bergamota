# Beryllium audio SMMU read-only probe

## Intent

Read the current Apps SMMU routing for Q6ASM's Beryllium stream ID `0x1821`
before designing or sending PCM DMA addresses. Linux identifies this stream in
the Q6ASM DAI node; Redwood shows that an existing bypass or translation must
be handled according to the mapping it already owns.

## Chosen approach

Add standalone boot mode `sweetadsp=6`. In `beryllium_adsp_probe()`, recognize
mode 6 before the existing PAS/GLINK mode validation, call the existing
read-only `sweet_smmu_probe_sid(0x1821, 0)`, log its returned classification,
and return. Level 0 reads global SMMU identification plus matching SMR/S2CR
entries; it does not read context-bank registers or write SMMU state.

The other approaches were extending the framebuffer sysctl to accept audio
SIDs, which would mix ownership between display and audio diagnostics, and
running the query inside mode 5, which would couple unrelated AFE and SMMU
results. The standalone mode keeps the observation isolated and useful even if
the AFE lifecycle is rejected.

## Scope and constraints

- Do not start PAS, load ADSP firmware, start GLINK/APR, configure AFE, or
  allocate audio buffers in mode 6.
- Do not call `sweet_smmu_map_sid()` or change SMR, S2CR, context-bank, or
  page-table state.
- Keep modes 1–5 unchanged.
- Report helper results: `0` no matching SMR, `1` bypass, `2` translated,
  `3` fault, `4` conflicting/mixed matches, `-1` probe unavailable or busy.
- If the probe reports an existing translation, do not infer ownership or
  choose a new IOVA from that result alone. Inspect its context only in a
  separately designed read-only step.

## Completion checks

Compile and execute the actual `beryllium_adsp_probe()` function extracted from
the ordered patch result, stubbing only its external board, boot-arg, ADSP and
SMMU calls. Verify mode 6 calls only the level-0 read-only helper and leaves
ADSP startup disabled, modes 1–5 retain their current flags, and a non-Beryllium
board does not probe. Compile the changed XNU translation unit locally, build
through GitHub Actions, then boot a temporary read-only image and capture the
classification. No partition write is part of this probe.
