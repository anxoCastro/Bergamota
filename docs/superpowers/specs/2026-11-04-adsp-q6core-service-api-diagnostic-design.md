# ADSP Q6Core service API diagnostics

## Intent

Use the already working, read-only `AVCS_CMD_GET_FWK_VERSION` exchange to expose the Q6Core service API versions returned by the Poco F1's ADSP. This answers which AFE, ASM, and ADM API versions the firmware advertises before we implement any audio command.

## Evidence

- The 1162 Poco capture receives a valid 148-byte Q6Core firmware-version response with firmware 2.8.0.0 and eight services. The APR v1 header is 32 bytes, so the version body begins at byte 32.
- Linux's Q6Core response defines each service record as three 32-bit values: service ID, API version, and API branch version. Its `q6core_get_svc_api_info()` searches this array for an individual service; the AFE, ASM, and ADM drivers use that helper during probe.
- The current XNU helper validates the exact service-array byte count but returns only the firmware version and service count. It discards the records, so the successful hardware query does not yet identify the AFE API version.
- Linux SDM845 DTS assigns service IDs 4 (AFE), 7 (ASM), and 8 (ADM) on `apr_audio_svc`. The Beryllium DTS connects the Multimedia1 ASM front end to audio routing and the AFE SlimBus0 RX port/ WCD9340 path; it also declares a Quaternary MI2S RX data line for the TAS2559/TAS2560 speaker amplifier path.
- postmarketOS's Beryllium package depends on the SDM845 UCM package, and its device update records that audio worked on the Poco F1. A Linaro presentation reports mainline audio playback, microphone, and headphone support on the device. These sources establish a Linux reference stack, not an XNU implementation recipe.

References: [Linux Q6Core response and service lookup](https://github.com/torvalds/linux/blob/master/sound/soc/qcom/qdsp6/q6core.c), [SDM845 service IDs](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845.dtsi), [Beryllium audio routes](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845-xiaomi-beryllium-common.dtsi), [Beryllium WCD9340 description](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/sdm845-wcd9340.dtsi), [postmarketOS Beryllium update](https://gitlab.com/postmarketOS/pmaports/-/merge_requests/1962), [Linaro Poco F1 status](https://static.linaro.org/connect/lvc21/presentations/lvc21-101.pdf).

## Design

- Extend the existing bounded Q6Core response structure to hold at most `512 / 12` service entries, matching the existing APR response payload limit and parser count check.
- Decode each entry only after the parser has validated the APR envelope, header size, service count, and exact packet length. Preserve unknown service IDs and duplicates as received; this patch reports protocol data without imposing new firmware assumptions.
- Log one bounded line per service with its ID, API version, and branch version. Do not add another APR request, open another GLINK endpoint, or send AFE/ASM/ADM opcodes.
- Keep all modes except explicit `sweetadsp=4` unchanged. Continue boot if the query or parsing fails.
- Use the current read-write-root diagnostic image flow for the RAM boot so the system can reach SpringBoard, but do not flash a partition or change GPT. Record the decoded service table from the kernel capture; screen visibility remains a separate observation.

## Alternatives considered

1. Send an AFE command immediately. Rejected because Linux obtains the AFE API version from the existing Q6Core table before it sends port configuration/start requests; the exact first AFE request and route are not yet selected.
2. Re-query Q6Core using another opcode or open a second channel. Rejected because the current response already contains the needed table.
3. Log only known IDs 3/4/7/8. Rejected for this diagnostic: logging every bounded record gives us the device's full service inventory and avoids silently hiding an unexpected service layout.

## Acceptance criteria

- The response helper exposes each validated service record without reading beyond the packet or increasing the existing service-count bound.
- Fixture checks cover a populated v1 service table, zero records, and a malformed/truncated table.
- Static contract checks ensure the new probe sends no command beyond the existing 20-byte Q6Core version request.
- The ordered patch series applies, focused and full local tests pass, and the pinned GitHub Actions XNU build succeeds with an empty `errors.txt`.
- A temporary `fastboot boot` image logs the Poco's complete service table, including any records for AFE 4, ASM 7, and ADM 8. No flash/GPT operation occurs.

## Licensing boundary

Linux sources are protocol references for the service record layout and device routing. The XNU parsing and logging remain independently implemented; do not copy Linux implementation code or add a co-author trailer.
