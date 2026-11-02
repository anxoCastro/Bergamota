# Beryllium ADSP SMP2P version negotiation

## Intent

Make XNU's Apps-to-ADSP SMP2P startup handshake follow the Linux Beryllium protocol after the ADSP publishes its inbound item. The latest captured boot has AP-owned item 429 at version 2 while remote-owned item 443 appears as version 1 with zero entries. Linux lowers its outbound version and kicks the mailbox when that happens; XNU currently does not.

The goal is to let the DSP complete the SMP2P handshake and publish slave-kernel, while preserving the existing boundary before GLINK/APR/audio startup.

## Goals and non-goals

- After validating a remote item 443 header, downgrade only AP-owned item 429 when its version is higher, preserve the current feature byte, and send mailbox bit 10 as Linux does.
- When versions match, intersect the AP feature bits with the remote feature bits and record negotiation completion.
- Log the version/feature negotiation so the next read-only capture proves which transition occurred.
- Keep item 443 read-only; do not change PAS arguments, QMP/XO lifetime, timeout, or error cleanup.
- Keep the next handset image root read-only to avoid further userdata writes.
- Do not add PAS SHUTDOWN, GLINK, APR, codec routing, or audio playback.

## Approaches

1. **Linux-compatible dynamic negotiation (selected).** Keep v2 as the initial outbound version when item 443 is absent, then downgrade item 429 and kick when a valid lower remote version appears. This matches the captured Linux driver behavior and the observed v2/v1 mismatch.
2. **Publish version 1 from the start.** This avoids the later mismatch but changes the initial capability advertisement without following Linux's selection rule and could conceal other compatibility issues.
3. **Add diagnostics only.** This is safest for shared state but cannot correct the concrete mismatch already present in the capture.

The selected approach is a narrow protocol correction. It writes only the AP-owned shared item 429, which this stage already initializes, and sends the same mailbox notification used during publication. It never writes remote-owned item 443.

## Constraints and risks

- Linux reference source is captured at commit 85f1df2a4ec71d7a91dd95a7a49f889d1595ffa8.
- Linux qcom_smp2p_negotiate() lowers the local version and kicks on a lower inbound version; once versions match, it masks local features by the remote feature set.
- The current XNU boot logs show item 429 v2 and item 443 v1/0 entries. This supports the mismatch hypothesis but does not prove it caused the generic SFR message or missing entry.
- Only update a structurally valid, nonzero remote version that is lower than the valid AP-owned version. Never upgrade the AP item in response to a higher remote version.
- Use memory barriers around the shared-memory write and mailbox kick. Leave QMP/XO and PAS behavior unchanged on all outcomes.
- A read-only Fastboot boot can still hit the known launchd mount failure after early ADSP startup; judge this change from the early UDP kernel log. Do not interpret a later mount panic as an ADSP regression.

## Verification

- Put the protocol transition in a small production C header and compile that exact header into a host test harness with literal v1/v2 fixtures. Add patch-integration checks for the AP-owned item write, mailbox kick, post-probe call order, and no stores to item 443.
- Build the XNU kernel through the fork's GitHub Actions workflow.
- Boot a temporary root-read-only image with UDP capture active before boot. Success evidence is an AP item-429 downgrade/kick followed by item 443 acquiring a valid slave-kernel entry; otherwise retain the failure log and reassess the hypothesis.
