# Beryllium ADSP APR/Q6Core Mode 4 Design

## Intent

Mode 3 now completes a native GLINK v1 negotiation on the Beryllium Apps/ADSP edge. This step adds one opt-in APR request to identify the ADSP firmware and service API versions. It uses only the `apr_audio_svc` channel and Q6Core service 3; it does not initialize AFE, ASM, an audio route, PCM, LPASS, or the codec.

The new `sweetadsp=4` mode runs the existing PAS start and Q6V5-ready gate, performs mode 3's GLINK base negotiation, opens the APR channel, advertises AP-owned receive intents, sends one header-only `AVCS_CMD_GET_FWK_VERSION`, and stops after a validated response or a bounded timeout. Modes 0–3 retain their existing behavior.

## Protocol evidence

- Linux SDM845 DTS binds the ADSP edge to `apr_audio_svc` and sets `qcom,intents = <512 20>`. The probe mirrors those receive-intent dimensions.
- A Beryllium mode-4 capture observed a peer OPEN on the same edge with remote ID 1, length 22, and a 32-byte aligned frame. Its NUL-terminated name is `fastrpcglink-apps-dsp`. Linux SDM845 DTS declares this as a sibling of `apr_audio_svc`; XNU has no FastRPC endpoint.
- **Inference:** consume one exact FastRPC sibling OPEN without ACK, then continue waiting for APR. This is the endpoint-driven GLINK OPEN behavior: the unimplemented sibling is not acknowledged or registered as an XNU channel. Reject duplicate FastRPC OPENs and all other unknown or malformed OPENs.
- A later Beryllium capture observed a second peer OPEN, remote ID 2, length 7, 16-byte aligned frame, name `IPCRTR`. Qualcomm's SDM845 vendor DTS declares `IPCRTR` as the `lpass` IPC-router GLINK transport; a neighboring SM8150 DTS lists it beside `apr_audio_svc` and `fastrpcglink-apps-dsp` on the ADSP edge.
- **Inference / next probe:** the XNU audio path has no ADSP QRTR endpoint, so consume at most one exact `IPCRTR` peer OPEN without ACK and continue waiting for direct `apr_audio_svc`. Do not send QRTR packets or claim that QRTR is initialized. Reject duplicates and malformed/unrecognized channels.
- Linux native GLINK sends an `OPEN` request with a NUL-terminated channel name, waits for both a matching remote `OPEN_ACK` and a remote `OPEN`, then acknowledges the remote's channel ID. It advertises local intents as `(size, id)` pairs and sends data through a remote intent ID.
- Linux Q6Core sends a 20-byte APR header-only sequential command with opcode `0x001292c`; it waits 100 ms for a response. A firmware-version response uses opcode `0x001292d` and carries five 32-bit version/count fields followed by three 32-bit fields per service.
- APR header size is 20 bytes. The request uses type `SEQ_CMD`, version 0, source domain APPS (5), destination domain ADSP (4), and service ID 3 at both ends. A basic APR result uses opcode `0x000110e8` and carries the original opcode and status.

References: [SDM845 DTS](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845.dtsi), [native GLINK protocol](https://raw.githubusercontent.com/torvalds/linux/master/drivers/rpmsg/qcom_glink_native.c), [GLINK SMEM transport](https://raw.githubusercontent.com/torvalds/linux/master/drivers/rpmsg/qcom_glink_smem.c), [Q6Core version query](https://github.com/torvalds/linux/blob/master/sound/soc/qcom/qdsp6/q6core.c), [APR wire header](https://raw.githubusercontent.com/torvalds/linux/master/include/linux/soc/qcom/apr.h).

## Goals

- Keep `sweetadsp=0`, `1`, `2`, and `3` behavior unchanged; only explicit `sweetadsp=4` requests APR.
- Enter APR only after PAS succeeds, Q6V5 ready is observed, and mode 3's base VERSION exchange completes.
- Open only the exact NUL-terminated channel name `apr_audio_svc` (14 bytes including NUL), local channel ID 1. Require both the matching peer `OPEN` and peer `OPEN_ACK`; send one `OPEN_ACK` for the peer ID. During this wait, consume at most one exact `fastrpcglink-apps-dsp` sibling OPEN without ACK.
- Mirror SDM845's `<512 20>` receive-intent parameters using AP-local buffers and IDs 1–20. Never write GLINK SMEM descriptor/FIFO indices other than AP-owned TX head and RX tail, or any remote-owned item.
- Parse remote intents only for the opened peer channel, with a bounded count and sizes. Send the APR request only when the peer has offered an intent large enough for 20 bytes; do not issue `RX_INTENT_REQ` in this version.
- Send exactly one APR `AVCS_CMD_GET_FWK_VERSION` request: 20 bytes, service 3, APPS 5 to ADSP 4, zero ports/token, opcode `0x001292c`.
- Accept only an unfragmented GLINK `TX_DATA` packet addressed to the opened channel and one of the advertised local intent IDs, with payload no larger than 512 bytes. Return `RX_DONE` for the consumed local intent.
- Validate the complete APR response header, domains, service, ports/token, exact packet size, response opcode, and service-array count before logging firmware/API versions. A basic result is accepted only when it names the query opcode. Reject malformed or unrelated packets.
- Bound the APR-channel handshake and response wait. Leave the negotiated channel and QMP/XO state intact at the end; do not send GLINK CLOSE or PAS shutdown.
- Continue boot after probe failure. Preserve the filesystem read-only and use temporary `fastboot boot` images only.

## Safety gates

1. The mode-4 path runs only after mode 3 returns a valid base VERSION_ACK; modes 0–3 are unchanged.
2. All incoming control frames are length-checked and 8-byte aligned before advancing RX tail. Unknown commands or malformed frames remain unread and terminate the probe. The exceptions are one exact, nonzero-ID `fastrpcglink-apps-dsp` OPEN and one exact, nonzero-ID `IPCRTR` OPEN; consume either without ACK, then keep waiting for APR. Duplicates remain unread and terminate the probe.
   For a rejected peer `OPEN`, diagnostics include its command, remote ID, `param2`, total frame length, and at most 32 bytes of frame data. Logging does not consume the frame or change the rejection path.
3. The only transmitted APR command is the zero-initialized 20-byte Q6Core version query. No AFE/ASM service ID or mutating opcode is sent.
4. RX data is accepted only for the exact channel ID, local receive-intent ID, single-fragment size fields (`left_size == 0`), and a bounded 512-byte buffer. The APR parser must validate the full envelope before reporting data.
5. On every timeout or malformed state, log the stopping gate, leave remote-owned fields untouched, do not reset rings, and continue XNU boot.

## Alternatives considered

1. Open APR and immediately query AVCS without publishing local receive intents. Rejected because the GET_FWK_VERSION reply needs a valid GLINK receive buffer under the SDM845 intent-based protocol.
2. Implement the complete APR bus plus AFE/ASM/audio routing. Deferred until a real Q6Core response confirms the transport and firmware protocol.
3. Query Q6Core through `q6core` instead of `apr_audio_svc`. Linux's SDM845 DTS advertises `apr_audio_svc` on this ADSP edge, so that is the channel used by the existing stack.

**Selected:** one APR channel, one read-only Q6Core version query, and a bounded response parser.

## Acceptance criteria

- Compiled helper tests cover APR request bytes, OPEN channel-name validation, bounded GLINK frame sizing, remote intent parsing, and valid/malformed firmware-version and basic-result APR responses.
- Static patch-contract tests prove modes 0–3 remain unchanged, mode 4 follows successful mode 3, only `apr_audio_svc`/service 3/Q6Core version opcode are used, intent ownership is local, and no AFE/ASM/PCM/codec or shutdown path is reachable.
- The ordered XNU patch series through 1150 applies; local helper builds use `-Wall -Wextra -Werror`; KDK-enabled GitHub Actions build succeeds with empty `errors.txt`.
- The temporary image has `sweetadsp=4`, the verified Beryllium ADT/Enxerto loader, and no filesystem write flags. Hashes and Fastboot product are verified before RAM boot.
- The device capture identifies channel-open progress, intent exchange, APR request and exact response or timeout. No partition or GPT operation is performed.
- Any claim about audio remains out of scope: a Q6Core version response establishes APR reachability, not audio playback.

## Licensing boundary

Linux sources are protocol references for wire values, channel sequencing, intent dimensions, and APR query layout. The XNU patch and helper are independently written. Do not copy Linux implementation code or add a co-author trailer.
