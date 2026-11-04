# Linux-faithful Beryllium QUAT AFE lifecycle

## Goal

Extend the opt-in ADSP diagnostics with a separate `sweetadsp=5` mode that
reproduces Linux's Beryllium QUAT MI2S AFE control-plane startup and shutdown:
configure the QUAT I-bit clock, configure the I2S port, start it, stop it,
and require a correctly correlated APR result for every operation.

The mode is a port-lifecycle smoke test. It is not PCM playback and must not
be described as working speaker audio.

## Evidence and inputs

- The Poco's observed Q6Core table advertises AFE service 4, API 2.0, branch
  0 exactly once. The current `sweetadsp=4` capture reaches that service and
  receives an AFE GET_PARAM_V2 status 2 response, but Linux does not use that
  query to start its I2S port.
- The Beryllium DTS maps QUAT MI2S RX to port `0x1006`, SD0 only, with the
  external TAS2559/2560 speaker amplifiers.
- The QUAT clock command uses token 22. In this board path,
  `sdm845_snd_startup()` reaches `q6afe_port_set_sysclk()`, which routes to
  `q6afe_set_lpass_clock_v2()` and uses the AFE port token. The separate global
  `q6afe_set_lpass_clock()` helper uses token 1024 but is not called for QUAT.
- Linux programs clock ID `0x106` at 1,536,000 Hz, then SET_PARAM_V2 with the
  full 36-byte `union afe_port_config`, then DEVICE_START. Shutdown sends
  DEVICE_STOP. The current QUAT machine shutdown branch does not disable the
  clock; follow that behavior for this experiment.
- The test configuration is minor version 1, 48 kHz, 16-bit, stereo, SD0,
  internal WS source, linear PCM, zero reserved and zero union padding. The
  rate/width/channel combination is a documented smoke-test vector, not a
  fixed DTS property.
- Full route and source details: `docs/audio/BERYLLIUM_LINUX_AUDIO_REFERENCE.md`.

## Design

1. Preserve modes 1–4. Mode 4 remains the read-only GET_PARAM diagnostic.
   Mode 5 is separately opt-in and performs the Linux-like AFE lifecycle.
2. Require the existing PAS/GLINK/APR startup and a unique advertised AFE
   API 2.0 record before sending any new AFE command.
3. Send service clock SET_PARAM (`0x100f3`) with the QUAT port token 22 and
   exact 20-byte clock data: minor 1, id `0x106`, 1,536,000 Hz, attribute 1,
   root 0, enable 1.
4. After a successful APR basic-result response, send port SET_PARAM_V2
   (`0x100ef`) for port `0x1006`, with module/parameter `0x1020c/0x1020d` and
   parameter size 36. The first 20 bytes encode the I2S vector; the remaining
   16 bytes are zero.
5. After success, send DEVICE_START (`0x100e5`) for port `0x1006`. After its
   success, send DEVICE_STOP (`0x100e6`). Require success for every stage and
   stop the sequence on a negative status, malformed response, timeout, or
   non-reusable remote intent.
6. Do not send PCM, ASM, ADM, codec, amplifier, pinmux, or GPIO operations.
   Do not disable the QUAT clock after stop in this first experiment because
   the current Linux QUAT shutdown path leaves it enabled. DSP reboot at the
   end of the boot resets the experimental state.
7. Repair the mode-4 GET parser to report a correlated nonzero AFE status
   without interpreting its trailing output bytes as I2S configuration. This
   is required so future GET diagnostics do not call a valid status response
   a malformed-length packet.

## Failure handling

- A clock enable rejection aborts before port configuration.
- A port configuration rejection aborts before start.
- A start rejection aborts before stop.
- A stop rejection is reported as an unresolved active-port state; do not
  continue by issuing a clock change.
- If an APR response or remote-intent completion cannot be correlated, stop
  the mode and leave DSP/GLINK ownership untouched. No retries or guessed
  fallback packets are allowed.
- No PAS shutdown or GLINK close is added; existing successful boot
  observations keep QMP/XO/ADSP on for the kernel lifetime.

## Acceptance

- Compiled helper tests assert exact bytes/lengths for the clock request
  encoder, the padded 36-byte I2S port configuration, start/stop packets,
  and APR basic-result correlation/status handling.
- The full SET_PARAM_V2 packet is 84 bytes (20 + 16 + 12 + 36); the clock
  packet is 68 bytes and start/stop are 24 bytes. The QUAT clock uses port
  token 22, not the global clock helper's token 1024.
- A fixture matching the captured 80-byte GET_PARAM rejection parses as
  status 2 and never yields configuration data.
- Static patch checks prove mode 4 remains query-only and mode 5 sends only
  clock SET_PARAM, port SET_PARAM_V2, DEVICE_START and DEVICE_STOP. No ASM,
  ADM, PCM, codec, GPIO or speaker-amplifier operation appears.
- Ordered patch replay and a local XNU translation-unit compile pass; the
  pinned XNU 26.5 GitHub Actions build succeeds.
- A temporary read-only `fastboot boot` capture shows each AFE request and
  its correlated status. No partition or GPT write is performed. The result
  is classified as AFE lifecycle control-plane success or a specific
  command rejection; no audible-audio claim is made.

## Licensing

Use Linux to establish packet formats, route selection, and board parameters.
Implement the XNU path independently and do not copy GPL ASoC/Q6DSP code.
Keep firmware and device dumps out of Git. Do not add co-author trailers.
