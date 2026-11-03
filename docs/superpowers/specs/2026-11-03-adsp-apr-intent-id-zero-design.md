# ADSP APR Remote Intent ID Zero

## Intent

Patch 1157 captured the exact frame that the `sweetadsp=4` probe rejects:

```text
05 00 05 00 01 00 00 00 80 00 00 00 00 00 00 00
```

It is a complete GLINK INTENT on remote channel 5 with one `(size=128, id=0)` pair. The rejection is caused by `beryllium_adsp_apr_parse_remote_intents()` treating remote ID zero as invalid. The local channel ID, frame length, count, and intent size pass their existing checks.

Qualcomm's Linux GLINK handler stores the wire ID and registers exactly `[id, id + 1)` with `idr_alloc`; Linux documents those bounds as minimum-inclusive and maximum-exclusive. That implementation accepts ID zero. This is strong transport evidence, not a formal firmware specification.

## Goals and constraints

- Accept remote intent ID zero in `beryllium_adsp_apr_parse_remote_intents()`.
- Preserve all existing count, frame-size, size-bound, channel-ID, and duplicate-ID validation.
- Keep AP-owned/local intent IDs 1–20 and TX_DATA response validation unchanged.
- Do not add GLINK commands, APR opcodes, or audio-device operations. Once this frame is accepted, the existing flow may send its existing single GET_FWK_VERSION request using the selected remote intent ID.
- Test the exact captured 16-byte layout and prove two zero IDs in one list are still rejected.
- Build with CI and test only through a temporary RAM boot; do not write partitions or GPT.

## Design

Remove only the `id == 0` disjunct from the remote intent entry bounds check. Retain `size == 0`, `size > 0x4000`, and the prior-entry equality loop. The existing TX_DATA encoder writes the chosen remote ID verbatim into its frame, and its receive-side validator constrains the AP-owned local intent ID separately; no transmit code needs to change.

The compiled C helper fixture will construct the captured command/channel/count/size/ID values, assert the parser returns one intent with ID 0 and size 128, then construct a two-entry list with duplicate ID 0 and assert rejection. Existing tests continue covering zero size, oversize entries, mismatched channel, and duplicate nonzero IDs.

## References

- [Qualcomm Linux GLINK handler](https://code.googlesource.com/linux/torvalds/linux/%2B/18ded910b589839e38a51623a179837ab4cc3789/drivers/rpmsg/qcom_glink_native.c)
- [Linux IDR allocator](https://code.googlesource.com/linux/torvalds/linux/%2B/18ded910b589839e38a51623a179837ab4cc3789/lib/idr.c)
