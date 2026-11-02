# Beryllium ADSP GLINK Mode 3 Design

## Intent

Patch 1148 established a read-only baseline after Q6V5 ready: descriptor item 478 is 32 bytes, AP TX FIFO item 479 is absent, remote RX FIFO item 480 is 16 KiB, and all four indices were zero and stable over 100 ms. The next step is an opt-in cold-start experiment that validates the Apps/ADSP SMEM state, creates only the missing AP-owned TX FIFO, and attempts the smallest native GLINK version exchange.

The existing `sweetadsp=2` path remains the PAS plus read-only survey path. A new `sweetadsp=3` explicitly requests the same PAS startup followed by this GLINK probe. Ordinary boot and modes 1 and 2 keep their current behavior.

## Evidence and protocol reference

- The pinned Linux `qcom_glink_smem.c` maps descriptor words as TX tail, TX head, RX tail, RX head; creates a 16 KiB TX FIFO if item 479 is missing; obtains the RX FIFO at item 480; and starts native GLINK after preparing AP-owned indices. The running handset survey found that exact missing/present item pattern.
- The pinned Linux `qcom_smem.c` uses TCSR hwspinlock 3 to protect the private allocation list. Private uncached item headers are 16 bytes, payload sizes are 8-byte aligned, and the allocator publishes the new free offset only after a memory barrier.
- Native GLINK version messages are 8 bytes: command (`VERSION=0` or `VERSION_ACK=1`), 16-bit version, and 32-bit feature mask. Version 1 with feature mask 0 requests only the base protocol. Linux's implementation responds to remote VERSION with VERSION_ACK and accepts VERSION_ACK as completion of a local request.
- The XNU modem transport is a separate global-SMEM edge with different host IDs and mailbox client. It is a protocol reference only; its shared transport state will not be reused.

References: [pinned Linux GLINK-SMEM transport](https://github.com/torvalds/linux/blob/85f1df2a4ec71d7a91dd95a7a49f889d1595ffa8/drivers/rpmsg/qcom_glink_smem.c), [pinned Linux SMEM allocator](https://github.com/torvalds/linux/blob/85f1df2a4ec71d7a91dd95a7a49f889d1595ffa8/drivers/soc/qcom/smem.c), [Linux native GLINK protocol](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/%2B/c7decec2f2d2ab0366567f9e30c0e1418cece43f/drivers/rpmsg/qcom_glink_native.c).

## Goals

- Preserve the read-only `sweetadsp=2` survey and add a separate explicit `sweetadsp=3` mode.
- After Q6V5 ready, require descriptor item 478 of exactly 32 bytes and an RX FIFO item 480 with a usable 8-byte-aligned size.
- Under TCSR mutex 3, revalidate the Apps/ADSP private SMEM partition and both allocation lists. If item 479 is absent, allocate one 16 KiB AP TX FIFO. If it exists, accept only an exact 16 KiB item. Never allocate item 480.
- Before sending, sample the descriptor twice 100 ms apart and again after allocation. Require all indices to remain zero, double reads to match, and FIFO bounds/alignment to be valid. Do not reset any descriptor field.
- Write only the AP-owned TX FIFO payload, descriptor TX head (word 1), and—only when consuming a recognized inbound version packet—descriptor RX tail (word 2). Never write TX tail, RX head, remote FIFO item 480, or remote SMP2P item 443.
- Send only the 8-byte GLINK VERSION v1/features-0 packet, ring APSS shared mailbox client 8, and poll the RX ring for at most 2 seconds.
- Consume VERSION or VERSION_ACK packets only after checking their complete 8-byte length and stable/range-valid indices. Respond to VERSION v1 with VERSION_ACK v1/features-0. Accept an ACK to our request only when it reports version 1 and features 0, matching the local offer. Stop without consuming on malformed, unsupported, non-version traffic, or feature mismatch.
- Keep APR, channels, intents, service registration, PCM, LPASS, and codec work outside this mode.
- Preserve firmware, root, GPT, recovery, modem, persist, and Android partitions. Hardware work remains `fastboot boot` only.

## Safety gates

1. The mode-3 branch is reachable only after successful PAS and the existing Q6V5 ready observation.
2. The SMEM lookup must distinguish absent from malformed and check both uncached and cached lists. Allocation occurs under hwspinlock 3. A malformed partition, duplicate item, wrong existing TX size, full partition, or lock timeout ends the probe without sending GLINK.
3. The descriptor and remote RX FIFO must be found and valid. All four indices must be in bounds, 8-byte aligned, and exactly zero in stable samples before first start. Any nonzero or changing value ends the probe without resetting it.
4. GLINK TX is a single 8-byte command, requires at least the 16-byte Linux ring reserve, writes with wrap-safe 32-bit SMEM access, publishes the AP TX head after a barrier, then rings mailbox bit 8.
5. Polling is bounded; no IRQ is enabled. Ring reads and parser are limited to the 8-byte VERSION/VERSION_ACK packet. Unknown command, malformed version, nonzero-length response, or timeout is logged and terminates this startup attempt.
6. No completion/failure path issues PAS SHUTDOWN, resets QMP/XO, or modifies a remote-owned field.

## Approaches considered

1. Extend the existing read-only survey to allocate the missing FIFO but stop before GLINK. This is safer to implement but does not check that the AP can reach the DSP through its mailbox and ring.
2. Add a separate cold-start mode that allocates only item 479, validates that the ring is empty without resetting it, sends one VERSION request, and polls for the bounded base-protocol response. This tests the transport while avoiding APR and preserving all remote-owned state.
3. Start the full `apr_audio_svc` channel and query AVCS. This combines ring initialization, version negotiation, channel setup, intents, service registration, and firmware-specific payloads before the transport is proven.

**Selected:** approach 2. It is an isolated opt-in protocol probe, preserves modes 1/2, and stops before any channel or audio service.

## Failure behavior

Every validation, lock, ring, and timeout error is diagnostic and returns from the GLINK probe while allowing XNU boot to continue. Do not attempt a speculative repair, rewrite nonzero indices, clear FIFO contents, consume unknown messages, or send an APR packet. Log which gate stopped the probe and whether item 479 was newly allocated or already present.

## Acceptance criteria

- Compiled C tests exercise descriptor/ring validation, ring wrap arithmetic, GLINK v1 packet decoding, unsupported messages, and mode selection.
- Static patch-contract tests establish that mode 2 remains read-only; mode 3 requires the ready branch; allocation is under TCSR lock 3; no remote-owned item/field is written; only mailbox client 8 is used; and the GLINK probe contains no APR path.
- The exact patch series through 1149 applies and the KDK-enabled GitHub Actions kernel build succeeds with empty `errors.txt`.
- The temporary image contains `sweetadsp=3`, uses the verified Beryllium ADT/Enxerto loader, and omits filesystem write flags. Verify hashes and Fastboot target before `fastboot boot`.
- The device capture shows whether item 479 was allocated, the exact mode-3 gate reached, the VERSION request/response or timeout, and any boot continuation. No partition/GPT operation is performed.
- Claims remain limited to the actual capture: a version exchange does not prove APR connectivity or audio playback.

## Licensing boundary

Linux is used to identify wire values, layout, ownership, and ordering. Implement the XNU code independently; do not copy GPL implementation code. Keep the MPSS transport and all unrelated devices untouched. No co-author trailer is added.
