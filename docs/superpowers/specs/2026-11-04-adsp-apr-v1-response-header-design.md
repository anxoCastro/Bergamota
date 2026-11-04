# ADSP APR v1 response-header support

## Intent and evidence

Patch 1161 boots through Q6V5 ready, negotiates GLINK, sends one Q6Core `GET_FWK_VERSION`, and receives the matching `RX_DONE_W_REUSE`. The next APR `TX_DATA` has a reported length of 148 bytes. Its logged 64-byte prefix starts with header field `0x0181`, packet size `0x0094`, response opcode `0x0001292d`, then body fields at offset 32: major 2, minor 8, branch 0, subbranch 0, and service count 8. The count implies `20 + 8*12 = 116` body bytes, and `32 + 116 = 148` total bytes.

The current parser requires the v0 20-byte header field `0x0150` and reads the body at offset 20, so it rejects this response. Linux APR defines the low header-field bits as version, header size in four-byte words, and message type; its receiver accepts versions through 1 and skips optional header bytes using the encoded length. Linux Q6Core defines the firmware-version body as five 32-bit words followed by 12-byte service records. References: [Linux APR header](https://github.com/torvalds/linux/blob/master/include/linux/soc/qcom/apr.h), [Linux APR receive parser](https://codebrowser.dev/linux/linux/drivers/soc/qcom/apr.c.html#L202-L261), [Linux Q6Core response layout](https://github.com/torvalds/linux/blob/master/sound/soc/qcom/qdsp6/q6core.c#L977-L1002).

The capture contains only the first 64 bytes, not the full 148-byte packet. Tests may construct the remaining service records synthetically while preserving the captured header, body prefix, and total-length relationship.

## Approaches

1. Accept only the observed `0x0181` field and hardcode a 32-byte body offset. This is small but ties parsing to one header length.
2. Decode APR version, message type, and header size, then parse the response body at that bounded offset. This follows Linux APR framing and retains support for the existing v0 fixtures. **Chosen.**
3. Accept arbitrary response header fields or relax exact payload-length checks. This admits formats without evidence and is unnecessary because the observed packet length matches the count-derived body exactly.

## Decision and constraints

- Accept APR header versions 0 and 1 only, and command/response message type 1 only.
- Require a decoded header length of at least 20 bytes and no longer than the packet.
- Keep the existing source/destination service, domain, and port checks; require the packet-size field to equal the received APR payload length.
- Parse both the basic-result payload and firmware-version body relative to the decoded header length. For firmware version, require the exact length `header_bytes + 20 + service_count*12` and keep the existing service-count bound.
- Ignore optional header extension bytes; do not alter the request, GLINK ownership, consume/ACK order, or startup sequence.
- Keep the single GET_FWK_VERSION request and the read-only `fastboot boot` workflow.

## Tests

- Add a compiled structural fixture using the captured 64-byte prefix and a synthetic 148-byte remainder; it must parse as firmware version 2.8.0.0 with eight services.
- Retain v0 firmware-version and basic-result tests.
- Reject unsupported versions, non-response message types, header lengths below 20 or beyond packet size, packet-size mismatch, and a service count inconsistent with the exact body length.
- Replay through patch 1162, run focused and full suites, then build and RAM-boot with a fresh bounded log capture.
