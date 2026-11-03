# ADSP APR Intent Frame Diagnostics

## Intent

The 1156 RAM boot reaches Q6V5-ready, completes GLINK v1 negotiation, opens `apr_audio_svc`, and advertises 20 AP-owned intents. It then stops on a remote GLINK command 5. The current log does not include the frame fields or payload, so it cannot show which parser condition rejects the frame.

Add a diagnostic-only log at that rejection point so the next RAM boot records the status, command, channel ID, parameter 2, validated frame length, available copied bytes, and the bounded raw bytes. Keep the current fail-closed behavior and do not consume or acknowledge a rejected frame.

## Goals and constraints

- Keep `sweetadsp=4` behavior unchanged except for one bounded failure log.
- Dump at most `ADSP_APR_GLINK_FRAME_MAX` (528) bytes already copied into the local buffer.
- Preserve the RX tail when the frame is malformed, unknown, or rejected by the intent validator.
- Do not relax GLINK/APR validation or send Q6Core/audio traffic in this diagnostic patch.
- Validate through the existing patch replay, source-contract tests, CI kernel build, and a temporary `fastboot boot` image when the host sees an unlocked `beryllium` target.
- Make no partition or GPT writes.

## Approaches considered

1. Log only command and header fields. This is small, but cannot identify which intent entry or wire-layout detail is invalid.
2. Log the copied frame in full, with a 528-byte cap. This preserves enough evidence to compare every bounded field while keeping the existing rejection semantics. **Selected.**
3. Relax the parser or consume command 5 unconditionally. This would hide the rejecting condition and may let an invalid intent list drive later memory or APR operations; it is outside this diagnostic goal.

## Design

In `adsp_apr_wait_remote_intent`, when frame status is not complete, the command is not INTENT, or `adsp_apr_record_remote_intents` rejects the frame, print the captured fields and raw bytes as hex. For a complete frame, dump exactly `frame_bytes`; when framing failed before a frame length was established, dump the copied bytes. Both lengths are bounded by `copy_bytes`, which is capped by the 528-byte local frame buffer. Return false as before and leave RX tail unchanged. Tests pin the logged fields, loop bound, and fail-closed ordering.

## Deferred work

After collecting a new frame, compare the actual command, channel ID, count, frame length, and every `(size, id)` pair against the parser and a primary GLINK reference. Only then propose a separate protocol change if the captured bytes establish one.
