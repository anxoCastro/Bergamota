# ADSP APR response: consume GLINK `RX_DONE_W_REUSE`

## Intent

The one-shot Q6Core `GET_FWK_VERSION` response wait must consume the peer's reuse acknowledgement for the remote intent used by the request. The Poco capture reports GLINK command 14 and then stops before a validated Q6Core response.

## Evidence

Linux `qcom_glink_native.c` assigns command 14 to `GLINK_CMD_RX_DONE_W_REUSE`; the existing local GLINK receive path also names value 14 `G_RX_DONE_W_REUSE`. It is an eight-byte control frame with the remote channel ID and intent ID. The current APR helper accepts command 6 (`RX_DONE`) as an eight-byte frame but rejects command 14. The hardware log does not include command 14's channel or intent fields, so the XNU handler must validate both against the outstanding request.

## Goals and constraints

- Accept command 14 as an eight-byte GLINK control frame.
- In the response wait, consume it only if its channel equals the opened remote `apr_audio_svc` channel, its intent ID equals the outstanding Q6Core request intent, and no completion was already recorded.
- Treat it as completion of the request's intent transmission while preserving the remote intent for reuse.
- Keep command 6 behavior, APR payload validation, the single outgoing query, and all other GLINK command handling unchanged.
- Fail closed on mismatched, duplicate, truncated, or oversized control frames.
- Device validation uses temporary `fastboot boot`; do not flash partitions or alter GPT.

## Approaches

1. Ignore command 14. Rejected because it leaves the frame at the RX tail and prevents the response wait from reaching the APR response.
2. Accept only command 14 with matching channel and outstanding intent ID, using the existing completion path. **Selected:** this handles the observed protocol message and preserves strict checks without adding general GLINK state.
3. Implement broad handling for all GLINK control commands. Rejected as out of scope; the capture identifies one specific completion command.

## Verification

Add a compiled eight-byte command-14 fixture with a seven-byte incomplete case, plus a response-wait contract check for channel/intent matching, duplicate rejection, and consumption ordering. Run the focused APR checks and full test suite, build via GitHub Actions, then RAM-boot and capture the Q6Core response and root/UI progress.
