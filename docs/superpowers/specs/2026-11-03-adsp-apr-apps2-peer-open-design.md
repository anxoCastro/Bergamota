# ADSP `apr_apps2` Peer OPEN Design

## Intent

Let the existing `sweetadsp=4` probe continue its bounded `apr_audio_svc` open wait after the phone offers the observed `apr_apps2` GLINK channel. This is needed to reach the probe's already-defined Q6Core firmware-version query.

## Evidence

The 1155 RAM-boot capture records `apr_audio_svc` opened as remote channel 5, followed by an OPEN for remote channel 6 with `param2=10`, a 24-byte frame, and wire name `apr_apps2\0`. The probe stops at that frame before sending Q6Core. Capture: `.superpowers/ci/41685674495/live-udp-adsp-apr-lpass-ignore.log`, SHA-256 `9f81dde06f81b59a368f6a6932919bff2fc1e76dd5f6d8cdc818598dc45284ee`.

Qualcomm's SDM845 device tree declares `apr_apps2` on the ADSP GLINK packet interface, separately from the `apr_audio_svc` APR channel. This XNU probe does not implement that packet endpoint. Source: [SDM845 device tree](https://android.googlesource.com/kernel/msm/%2B/3242618979ee08bc3b7fdac5d3950f7d702be0cd/arch/arm64/boot/dts/qcom/sdm845.dtsi).

## Goals and non-goals

- Recognize at most one peer OPEN whose command, frame size, name length, NUL-terminated name, and nonzero remote ID match the observed `apr_apps2` frame.
- Consume the AP-owned RX frame without sending `OPEN_ACK`, including if it arrives after `apr_audio_svc` has opened.
- Keep rejecting malformed frames, duplicates, and unknown channel names.
- Do not implement the `apr_apps2` packet endpoint, QRTR, SSR, FastRPC, APR service discovery, playback, capture, codec, or amplifier behavior.
- Do not change PAS, SMP2P, GLINK base negotiation, the Q6Core request format, or root filesystem configuration.

## Approaches

1. Implement the complete `apr_apps2` packet endpoint. This would add a separate transport surface beyond the current probe and does not match its immediate Q6Core goal.
2. Consume only the exact observed OPEN without ACK and continue waiting for `apr_audio_svc`. **Chosen:** this is the smallest experiment consistent with the observed peer sequence while keeping the unimplemented endpoint closed.
3. Keep rejecting `apr_apps2`. This preserves current behavior but prevents the probe from reaching its existing Q6Core request on this firmware.

## Constraints and validation

Use TDD for the exact validator and open-wait branch. The validator must require `OPEN`, a 24-byte frame, a 10-byte name including NUL, exact `apr_apps2\0`, and a nonzero frame remote ID matching the argument. Driver tests must prove that one matching OPEN is consumed without ACK even after `apr_audio_svc` opens, while a duplicate or malformed/unknown OPEN still stops the probe. Apply patches 1131–1156 in order, build through GitHub Actions, and package any follow-up device image as a read-only RAM boot; do not flash a partition.
