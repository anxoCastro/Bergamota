# Beryllium read-only AFE query

## Goal

Extend the explicit `sweetadsp=4` diagnostic far enough to send one AFE query after the already validated Q6Core version exchange. This checks that the Poco F1 firmware accepts an APR request addressed to its advertised AFE service and exposes the board's QUAT MI2S I2S configuration, without starting an audio route.

## Evidence

- The 1163 Poco capture reports AFE service ID 4, API 2.0, branch 0 in a complete Q6Core service table.
- Linux's SDM845 DTS identifies service ID 4 as Q6AFE. The Beryllium DTS assigns `QUATERNARY_MI2S_RX` one SD0 line and comments that it is the I2S path for the TAS2559/2560 amplifiers. Linux maps that DAI to AFE port `0x1006`; its DAI token is 22.
- Linux's Q6AFE driver reads the service API table during probe. Its playback path later configures a port with `AFE_PORT_CMD_SET_PARAM_V2` and starts it with `AFE_PORT_CMD_DEVICE_START`; this diagnostic deliberately does neither.
- Qualcomm's APR audio header defines `AFE_PORT_CMD_GET_PARAM_V2` (`0x000100F0`) and `AFE_PORT_CMDRSP_GET_PARAM_V2` (`0x00010106`). It permits in-band parameter responses when the shared-memory map handle is zero. The I2S parameter is module `0x0001020C`, parameter `0x0001020D`, with a 20-byte configuration structure. Qualcomm's downstream GET_PARAM example sizes the in-band payload as the 12-byte parameter header plus the requested parameter bytes.

References: [SDM845 DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845.dtsi), [Beryllium DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845-xiaomi-beryllium-common.dtsi), [Linux Q6AFE driver](https://github.com/torvalds/linux/blob/master/sound/soc/qcom/qdsp6/q6afe.c), [Qualcomm APR audio-v2 header](https://android.googlesource.com/kernel/msm/%2B/de04dbd97b900a596ed3b233409d5a0f9666f7b7/include/sound/apr_audio-v2.h), [Qualcomm GET_PARAM example](https://android.googlesource.com/kernel/msm/%2B/ce0b78dbf16e78d6726fb24c71b70f617bc6048a/sound/soc/msm/qdsp6v2/q6afe.c).

## Request and response

- Send exactly one APR v0 `AFE_PORT_CMD_GET_PARAM_V2` request over the existing `apr_audio_svc` GLINK channel, addressed to service ID 4 (AFE), with source/destination ports zero and token 22 (the Linux QUAT MI2S DAI token).
- Query port `0x1006`, module `0x0001020C`, parameter `0x0001020D`. Set the in-band maximum payload to 32 bytes (12-byte parameter header plus 20-byte I2S configuration), with zero physical addresses and memory-map handle.
- Require the advertised AFE record `(4, 2, 0)` before sending. Do not infer an API from a different record or send if it is missing, duplicated, or has another version.
- Accept only a bounded response whose APR header, service/domain/ports/token, packet length, opcode, parameter header, and 20-byte payload match the request. Also recognize a correctly correlated APR basic-result error response for opcode `0x000100F0` and log its status. Do not interpret an error as an audio configuration.
- Correlate and consume one peer `RX_DONE_W_REUSE` for the request's remote intent and one local response intent before reporting the exchange complete. Preserve the existing one-shot timeout and bounds.

## Safety boundary

- `GET_PARAM_V2` is the only new AFE opcode. It is a query; no AFE `SET_PARAM`, `DEVICE_START`, `DEVICE_STOP`, clock vote, GPIO/pinctrl, codec, ADM, ASM, PCM, DMA, amplifier, or speaker-protection command is sent.
- Keep the existing Q6Core query first and reuse the already-open APR endpoint and intents. Do not open a new GLINK endpoint or change PAS/QMP/XO teardown behavior.
- If the query is rejected, malformed, or times out, log the outcome and stop this diagnostic. Do not send a fallback command.
- Boot the device with a temporary RAM image and read-only userdata arguments. Do not run fsck, rootrw, or write any partition/GPT; prior HFS corruption remains unresolved.
- A successful query proves only AFE request/response reachability and readback of this parameter. It does not prove speaker playback or a complete audio route.

## Alternatives considered

1. Start QUAT MI2S RX with Linux's parameters. Rejected for this milestone because starting it activates the audio hardware, while clock provider, rate, bit width, codec readiness, and amplifier behavior have not been established on this iOS boot.
2. Query a Slimbus/WCD9340 port. Rejected because that would cross into codec and Slimbus state before AFE service reachability is known.
3. Send another Q6Core query. Rejected because the service table already established the installed AFE API and the goal is to exercise the AFE service itself.

## Acceptance

- A compiled helper fixture validates the exact GET request bytes and accepts a captured-shape v1 AFE response plus a correlated APR basic-result response; malformed headers, lengths, service IDs, tokens, parameter IDs/sizes, and duplicate completion fail closed.
- Static contracts prove the only new outbound AFE opcode is `AFE_PORT_CMD_GET_PARAM_V2` and no port-start, configuration, clock, ASM/ADM, codec, or PCM operation is introduced.
- The ordered XNU patch series through 1164 applies cleanly. Focused and full repository suites pass, and a pinned XNU 26.5 GitHub Actions build completes with empty `errors.txt`.
- A temporary read-only `fastboot boot` capture records either a valid AFE I2S parameter response or a bounded correlated AFE/APR error. No partition or GPT write occurs.

## Licensing boundary

The Linux and Qualcomm sources establish protocol fields and Beryllium hardware facts. Keep the XNU implementation independent; do not copy GPL driver code into this APSL tree. Credit upstream in `README.md` if code is later adapted. Do not add co-author trailers.
