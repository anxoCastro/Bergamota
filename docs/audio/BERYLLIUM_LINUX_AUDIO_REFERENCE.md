# Linux audio path on Poco F1 (Beryllium)

This note follows the SDM845 mainline-derived Linux tree at tag
`sdm845-6.18.2-r0` and its `alsa-ucm-conf` Beryllium profile. It records the
hardware route and protocol sequence to port independently to XNU. The Linux
ASoC/Q6DSP implementation is GPL; this document is a behavioral reference,
not code to copy into the APSL XNU tree.

## Board routes

The Beryllium common DTS describes two relevant playback backends:

- **Internal speaker:** Q6AFE `QUATERNARY_MI2S_RX` (`0x1006`) with SD0 only,
  routed through `q6routing` to the external TAS2559/TAS2560 smart amplifier.
  The sound node selects the QUAT MI2S and SD0 pinmux groups.
- **Wired headphones and analog capture:** WCD9340 through SLIMbus. The
  playback link uses `SLIMBUS_0_RX`; capture links use `SLIMBUS_0_TX` and
  `SLIMBUS_1_TX` for separate codec interfaces.

The DTS also declares Q6ASM Multimedia front ends and a Q6 Voice mode. Those
are distinct from the AFE backend: the AFE port alone is not a PCM stream and
cannot produce audio without a front end, ADM route, DMA buffers, codec or amp
configuration, and an iOS-facing audio provider.

## Linux QUAT MI2S startup

The Beryllium machine driver configures the QUAT CPU DAI as I2S with normal
bit/frame clock polarity. The CPU is the bit-clock and frame-clock provider;
the codec consumes those clocks. It programs the QUAT I-bit clock at
1,536,000 Hz using clock ID `0x106`.

Q6AFE then prepares the I2S port from the active PCM stream and DTS line mask:

- minor version `1`;
- bit width and sample rate from stream parameters;
- linear PCM (`0`);
- SD0 channel mode (`1`) from `qcom,sd-lines = <0>`;
- internal word-select source (`1`) when the CPU is clock provider;
- mono/stereo field from the stream's channel count;
- reserved field zero.

Linux does not use a fixed sample rate in the Beryllium DTS. For the XNU
control-plane smoke test, the chosen representative test vector is 48 kHz,
16-bit, stereo. That is an explicit test assumption, not a board constant.

Linux's packet order is:

1. Clock configuration: APR opcode `AFE_SVC_CMD_SET_PARAM` (`0x100f3`),
   token 22, module `0x1028f`, parameter `0x10290`; payload is the 20-byte
   clock structure: minor version 1, clock ID `0x106`, 1,536,000 Hz,
   attribute 1 (no coupling), root 0, enabled 1.
2. Port configuration: APR opcode `AFE_PORT_CMD_SET_PARAM_V2` (`0x100ef`),
   token 22, port `0x1006`, module `0x1020c`, parameter `0x1020d`.
3. Port start: APR opcode `AFE_PORT_CMD_DEVICE_START` (`0x100e5`), token 22,
   port `0x1006`.
4. On stream shutdown, `AFE_PORT_CMD_DEVICE_STOP` (`0x100e6`) is sent for
   port `0x1006`. The current Beryllium machine driver's QUAT shutdown branch
   does not send a clock-disable command; the first XNU smoke test follows
   that observed behavior and leaves the configured clock alone until DSP
   reboot.

### ABI details that matter

The I2S structure contains 20 meaningful bytes. Linux stores it inside
`union afe_port_config`, whose largest member is the 36-byte TDM structure.
The union is zero-initialized and `q6afe_port_start()` passes
`sizeof(port->port_cfg)`, so the SET_PARAM_V2 parameter size is **36 bytes**:
20 I2S bytes followed by 16 zero bytes. The parameter header is 12 bytes and
the port command header is 16 bytes, making the complete APR packet 96 bytes.

The clock SET_PARAM uses a 16-byte service command header, a 12-byte parameter
header, and the 20-byte clock structure: 80 bytes total. Device start and stop
packets are 36 bytes total (32-byte APR header plus a 4-byte port command).
Linux waits for and correlates an APR basic-result response for each command.

## What the XNU probe observed

The read-only `sweetadsp=4` probe established Q6Core firmware 2.8.0.0 and one
AFE API record `(service 4, API 2, branch 0)`. It sent a GET_PARAM_V2 query for
the QUAT I2S config. The DSP returned an 80-byte APR packet with status `2`
(`ADSP_EBADPARAM`); the response body included a zero parameter size and a
32-byte zero-filled output region. The current parser rejected that shape as a
length mismatch before reporting the status. This establishes that the query
was declined; it does **not** establish why, and it does not show that the
Linux SET_PARAM/start path is unsupported.

The important correction is that GET_PARAM is not part of Linux's port-start
path. Linux builds the configuration from the PCM stream and Beryllium DTS,
sets the clock, sends SET_PARAM_V2 with the full union, and starts the port.
The next XNU test therefore follows that sequence instead of trying another
query-size variation.

## XNU port boundary and later stages

The first Linux-faithful XNU experiment is an explicit boot-arg-only AFE
control-plane smoke test. It checks correlated success responses for clock
setup, QUAT configuration, start, and stop. It sends no PCM data and does not
open Q6ASM/Q6ADM, configure WCD9340 or TAS firmware, manipulate GPIO/pinctrl,
or expose an iOS audio device. The test vector may enable the QUAT clock and
port, but it must never be described as audible playback.

The later speaker path needs, at minimum, Q6ASM buffer setup and write/run,
Q6ADM/COPP matrix routing to the QUAT AFE backend, the Beryllium TLMM pinmux,
TAS2559/TAS2560 firmware and calibration/configuration, and an iOS 26
AudioServerPlugIn/HAL integration. The headphone/microphone path additionally
requires the WCD9340 and SLIMbus route. Each endpoint has separate hardware
and firmware prerequisites; the Linux topology is not proof that each
profile is working in this XNU port.

## Primary source references

- [Beryllium common DTS](https://gitlab.com/sdm845-mainline/linux/-/blob/sdm845-6.18.2-r0/arch/arm64/boot/dts/qcom/sdm845-xiaomi-beryllium-common.dtsi)
- [Q6AFE core driver](https://gitlab.com/sdm845-mainline/linux/-/blob/sdm845-6.18.2-r0/sound/soc/qcom/qdsp6/q6afe.c)
- [Q6AFE DAI driver](https://gitlab.com/sdm845-mainline/linux/-/blob/sdm845-6.18.2-r0/sound/soc/qcom/qdsp6/q6afe-dai.c)
- [SDM845 machine driver](https://gitlab.com/sdm845-mainline/linux/-/blob/sdm845-6.18.2-r0/sound/soc/qcom/sdm845.c)
- [Q6ASM DAI driver](https://gitlab.com/sdm845-mainline/linux/-/blob/sdm845-6.18.2-r0/sound/soc/qcom/qdsp6/q6asm-dai.c)
- [Q6ADM driver](https://gitlab.com/sdm845-mainline/linux/-/blob/sdm845-6.18.2-r0/sound/soc/qcom/qdsp6/q6adm.c)
- [Q6 routing driver](https://gitlab.com/sdm845-mainline/linux/-/blob/sdm845-6.18.2-r0/sound/soc/qcom/qdsp6/q6routing.c)
- [Beryllium ALSA UCM profile](https://gitlab.com/sdm845-mainline/alsa-ucm-conf/-/blob/sdm845-phones/ucm2/Xiaomi/beryllium/HiFi.conf)
