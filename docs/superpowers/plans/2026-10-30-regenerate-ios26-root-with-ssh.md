# Regenerate the iOS 26 Root with SSH Access Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to carry out this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a fresh iOS 26.6.2 root image that authorizes this host's public SSH key, so the Poco F1 can receive the ADSP firmware over USB after boot.

**Architecture:** Use the existing Rust Renegade root builder on the iPhone12,1 iOS 26.6.2 (23G90) IPSW, with a verified `berga-tools` bundle that contains Dropbear, dash, and toybox. Pass `/home/castro/.ssh/id_ed25519.pub` to the builder and save the sparse image under ignored `.superpowers/` storage. Build and inspect the image locally first; installing it is a separate destructive step that replaces the contents of `userdata` and requires fresh explicit user approval.

**Tech Stack:** `tools/renegade-rs` (`renegade-rs root-ipsw`), `tools/renegade/berga_tools.py`, Apple IPSW range source, Android sparse HFSX output, Fastboot.

**Spec:** `docs/IOS26_PORT.md` and the active ADSP handoff in `NOTES.md`.

## Global Constraints

- Build from iOS 26.6.2 build `23G90`, target `iPhone12,1` / `N104`.
- Include only the current public key; never read, copy, or publish the private SSH key.
- Keep firmware, IPSW components, build outputs, device dumps, and identifiers outside Git.
- Keep the bootloader, recovery, modem, persist, backups, and GPT unchanged.
- Do not flash `userdata`, mount or repair the current volume, or start PAS until each required action has fresh explicit user approval.
- Do not start PAS, GLINK/APR, or an audio route in this image-rebuild task.

## Review Focus

- **Wrong IPSW/device build:** resolve the IPSW URL for target `iPhone12,1` and require the builder to report `26.6.2` / `23G90`; stop on any mismatch.
- **Tools cache lacks SSH:** the local cache is `berga-tools-v1`, which predates SSH; fetch a release with `ssh/dropbear`, `ssh/dash`, and `ssh/toybox`, and require the release manifest/hash checks to pass before building.
- **Wrong SSH identity or path:** verify the built HFS image's `private/var/root/.ssh/authorized_keys` contains the current public key and that the dropbear launch job is present.
- **Image clobber or incomplete output:** use a new ignored output path, then record its size and SHA-256 and inspect the HFS catalog before any device write.
- **Unapproved userdata replacement:** before any flash, show the user the exact image digest, target partition, and expected loss of the current iOS root/user data; proceed only after explicit approval and a `beryllium` Fastboot product check.

---

### Task 1: Prepare pinned root-build inputs

**Files / artifacts:**
- Create under ignored storage: `.superpowers/ios26-root-rebuild/`
- Read: `tools/renegade/targets.py`, `tools/renegade/berga_tools.py`, `tools/renegade-rs/README.md`
- Use: `/home/castro/.ssh/id_ed25519.pub`

- [x] Confirm the target profile resolves to `iPhone12,1`, iOS `26.6.2`, build `23G90`.
- [x] Resolve the official IPSW URL for `iPhone12,1` / `26.6.2` / `23G90`; the Rust builder accepts a file or URL and reads archive members by range, so a full IPSW download is not required.
- [x] Fetch a `berga-tools` release with `tools/renegade/berga_tools.py` into the ignored work directory. The current local v1 cache cannot be used for SSH. Require its manifest/hash checks to pass, then create a zip from the verified release directory with `berga-tools.json` and the original relative paths at the archive root.
- [x] Preserve the downloaded/verified inputs only under `.superpowers/`; record source/build identifiers and the tools bundle checksum in the handoff.

**Check:** target values match exactly; the bundle manifest passes its SHA-256 checks and contains `ssh/dropbear`, `ssh/dash`, and `ssh/toybox`; the public-key fingerprint is `SHA256:01rQdgsXlkx/HtH59xKz3jFXTBJ9DzuRtQOgiFmouvs`.

### Task 2: Build a separate sparse root image

**Output:** `.superpowers/ios26-root-rebuild/ios26-root-ssh.simg`

- [x] Build `renegade-rs` from `tools/renegade-rs/Cargo.toml` in release mode.
- [x] Run `cargo run --release --manifest-path tools/renegade-rs/Cargo.toml -- root-ipsw <23G90 IPSW URL> .superpowers/ios26-root-rebuild/ios26-root-ssh.simg --tools .superpowers/ios26-root-rebuild/berga-tools.zip --ssh-key /home/castro/.ssh/id_ed25519.pub --size 24g`.
- [x] Confirm the build log reports the exact target and completes its copy, tweak, Mach-O, cache, and sparse-image stages. Keep the existing device untouched.

**Check:** output exists at the new path, is an Android sparse image, and its SHA-256 and byte size are recorded. No output is written to a device partition.

### Task 3: Inspect the image's SSH setup offline

- [x] Use `renegade-rs hfs <image> cat private/var/root/.ssh/authorized_keys` to compare the embedded public key with the local `.pub` file.
- [x] Use `renegade-rs hfs <image> stat private/var/root/.ssh/authorized_keys` and `ls` on the relevant launch-daemon directory to confirm root ownership/mode and `org.sweet.dropbear.plist` presence.
- [x] Record the final image checksum and the offline checks in `NOTES.md`.

**Check:** the authorized key fingerprint matches the host key; Dropbear, dash, toybox, and the launch job are present; no private key bytes are in the image or Git.

### Task 4: Install only after fresh approval

- [x] Present the image's checksum/size and explain that flashing replaces the current contents of `userdata` (including the current iOS root and its data).
- [x] After explicit approval, verify `fastboot getvar product` is `beryllium`, then flash only `userdata`; this replaces the current iOS root and its user data. Do not alter GPT, boot, recovery, modem, persist, or backups. The first attempt failed at sparse chunk 19/34 after chunks 1–18 wrote successfully. After reconnection through another hub, the retry completed all 34 chunks with `OKAY` (`fastboot flash` exit 0, 429.785 s); no reboot was sent.
- [x] The user authorized one `rootrw=1` temporary boot on 2026-10-31. The correctly packaged Enxerto+payload boot reached SpringBoard; root SSH worked. Copied and checksum-verified the MDT, `.b00`–`.b14`, `adspr.jsn` and `adspua.jsn` at `/usr/standalone/firmware/sweet/`; see exact sizes/hashes and boot log in `NOTES.md`. This boot's preflight ran before the copy and reported firmware missing. No filesystem inconsistency messages appeared in this capture.
- [ ] After the copy, boot only the prepared read-only payload (no `ufsrw=1`/`rootrw=1`) to run the opt-in ADSP firmware metadata preflight. PAS remains disabled. Record exact boot/log results in `NOTES.md`.

**Check:** no partition other than the explicitly approved `userdata` target is written; the preflight log reports metadata validation and still shows PAS disabled.
