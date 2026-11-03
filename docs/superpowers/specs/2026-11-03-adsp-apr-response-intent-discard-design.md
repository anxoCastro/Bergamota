# ADSP APR response wait: discard later INTENT advertisements

## Intent

Keep the one-shot Q6Core `GET_FWK_VERSION` response wait moving when the peer advertises more GLINK INTENT buffers after the request has already been sent. The 1158 live capture records 64 advertisements, then rejects another remote INTENT; the response wait currently appends every advertisement to a fixed 64-entry state array.

The capture did not retain the rejected frame's bytes, so it does not prove whether the final rejection was caused by total capacity, a repeated ID, or malformed fields. The bounded persistent accumulator is nevertheless unnecessary in this phase: the probe sends one query and never uses newly advertised intents for another outbound request.

## Goals and non-goals

- Parse each post-request INTENT with the existing channel, count, size, and within-frame uniqueness checks.
- On successful validation, consume the complete frame from the AP-owned RX tail and discard the temporary parsed entries.
- Do not increase or mutate `state->remote_intent_count` during response polling.
- On parser rejection, fail closed and log the saved remote-intent count plus only the bounded received frame bytes.
- Keep RX_DONE, TX_DATA, Q6Core response parsing, and outgoing request behavior unchanged.
- Do not add a new outbound request, restart, or shutdown path.

## Approaches

1. **Validate into a temporary array, then discard (selected).** This matches the one-request state machine, removes cumulative-capacity and cross-advertisement duplicate accumulation from the response phase, and preserves per-frame validation. The trade-off is that later peer intents cannot be reused; this probe has no second request to use them.
2. **Increase the persistent array or add a larger ring.** This retains state the current response phase does not use and only moves the finite limit.
3. **Consume every complete INTENT without parsing it.** This avoids capacity pressure but would silently accept malformed channel/count/size/duplicate data.

## Design

In `adsp_apr_poll_response`, parse the INTENT frame into a local `beryllium_adsp_glink_intent` array sized to `BERYLLIUM_ADSP_APR_MAX_REMOTE_INTENTS`. Only after the parser succeeds, call `adsp_glink_rx_consume_frame`; do not call `adsp_apr_record_remote_intents`. If parsing fails, emit a bounded hexadecimal dump (no more than `copy_bytes`, itself capped by the 528-byte local frame buffer), include `state->remote_intent_count`, and return false without consuming the frame. Preserve RX_DONE and TX_DATA branches byte-for-byte where practical.

## Verification

Add a driver-contract regression against the applied patch series. It must fail against the current response branch, then pass when the branch validates into temporary storage, consumes only after successful parsing, does not call the persistent recorder, and leaves persistent count unchanged. Run focused ADSP tests and the full Python test suite. Build through GitHub Actions, validate checksums and a read-only RAM image, then use `fastboot boot` only if an unlocked `beryllium` target is present. Record the exact response and mount/UI evidence without inferring success from missing log lines.
