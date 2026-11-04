# ADSP APR rejected-response diagnostics

## Evidence

Patch 1160 accepts the matching GLINK `RX_DONE_W_REUSE` for request intent 0. The next boot log reports `APR payload failed header/opcode/length validation; response intent released`, so a response `TX_DATA` reaches the APR parser but fails validation. The current diagnostic does not include its bytes or parsed fields.

Linux `q6core.c` models the `GET_FWK_VERSION` body as five 32-bit words followed by 12-byte service records, with the count in the fifth word. This is a reference for the expected body shape, not proof of the exact wire packet emitted by this Poco's firmware: https://android.googlesource.com/kernel/msm-extra/+/48696dd1a1e9bf42b32045253819137717020aa2/dsp/q6core.c

## Decision

When the APR parser rejects a valid GLINK `TX_DATA` frame, log the payload length and a hex prefix capped at 64 bytes. Keep the frame validation, RX-tail advance, `RX_DONE`, parser result, and failure return behavior unchanged. Do not log the complete 512-byte maximum payload and do not accept any new response form until the captured bytes identify a specific mismatch.

## Tests

- Compile checks for the diagnostic prefix length at 0, 1, 64, 65 and 512 bytes.
- Contract check that malformed APR response logging uses the bounded count and reads only `payload[i]` in that loop before returning failure.
- Replay patches through 1161 and run focused plus full test suites.
- Build the kernel in GitHub Actions and RAM-boot a read-only image to capture the actual payload header/opcode/length. Record mount/UI evidence separately.

## Constraints

- Exactly one Q6Core `GET_FWK_VERSION` request remains.
- No changes to APR acceptance rules, remote INTENT retention, or ADSP startup sequencing.
- Device validation uses `fastboot boot` only; omit `ufsrw=1` and `rootrw=1`; do not write partitions or GPT.
- Keep the raw device capture and firmware artifacts out of git.
