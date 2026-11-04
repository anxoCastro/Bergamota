# AFE GET_PARAM diagnostic plan

## Task 1: Extend the APR helper and regression fixture

- [x] Add a bounded request encoder for AFE service 4, `GET_PARAM_V2`, QUAT MI2S RX port `0x1006`, token 22, audio-device-interface I2S config `0x1020C/0x1020D`.
- [x] Add a strict response parser for the correlated `GET_PARAM_V2` response and APR basic-result failure; preserve APR v0/v1 header handling and all existing bounds.
- [x] Generalize mode 4 response polling only as needed to match one request intent completion and one response before sending the next command.
- [x] Add compiled helper fixtures and static contract coverage for request bytes, response bounds, strict service/token/parameter validation, and absence of active audio commands.
- [x] Replay patches 1163 and 1164 with `git apply --whitespace=error` over the exact 1162 CI build-tree baseline; run focused ADSP tests (48/48) and the full test suite (52/52).

## Task 2: Review, commit, and build

- [x] Review the generated 1164 patch for APR header fields, packet lengths, bounded reads, intent ownership, and accidental active-route commands.
- [ ] Commit the implementation and documentation without a co-author trailer; push the branch.
- [ ] Dispatch the KDK-enabled XNU 26.5 build with `gh`; verify the pinned SHA, patch application, successful build, artifact checksums, and empty `errors.txt`.

## Task 3: Query the AFE on Beryllium

- [ ] Package a read-only image from the successful kernel artifact and known loader/ADT; unpack and byte-compare components and confirm `ufsrw=1`/`rootrw=1` are absent.
- [ ] Capture logs before boot, verify one unlocked `beryllium` Fastboot target, and use `fastboot boot` only.
- [ ] Record the exact AFE response/error, capture hash, boot result, and final Fastboot/USB state in `NOTES.md` and `docs/audio/BERYLLIUM_ADSP_APR_DESIGN.md`.
- [ ] Commit and push the completed handoff. Do not fsck or run rootrw; do not write partitions or GPT.
