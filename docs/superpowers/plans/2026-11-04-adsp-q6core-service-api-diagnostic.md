# Q6Core service API diagnostics plan

## Task 1: Extend the bounded Q6Core response parser

- [x] Add a fixed-size service record type and bounded array to the APR response structure.
- [x] Decode `(service_id, api_version, api_branch_version)` records only after the existing header/count/exact-length checks pass.
- [x] Extend the compiled helper fixture to assert known records, zero records, and malformed/truncated lengths.
- [x] Add patch contract coverage proving no second command or AFE/ASM/ADM opcode is introduced.
- [x] Replay the patch series through 1163 and run focused (47/47) and full (50/50) local suites.

## Task 2: Review, CI, and package the diagnostic image

- [x] Independent review of `36e9d17..656aae6` found no Critical/Important or functional findings; note the stored unified-diff context whitespace reported by whole-range `git diff --check`.
- [x] Commit and push without co-author trailers; dispatch GitHub Actions run `41844885356` against `e441ddce2562aa0144f2386253ccce68e543cf42`.
- [ ] Verify the artifact checksums, empty `errors.txt`, patch application, and build completion.
- [ ] Package a RAM-boot image with the known ADT/loader and rootrw args, then unpack and byte-compare its components.

## Task 3: Read the Poco's Q6Core service table

- [ ] Confirm one unlocked `beryllium` Fastboot target and capture UDP kernel logs on port 5140.
- [ ] Run `fastboot boot` only; do not write partitions or GPT.
- [ ] Record each service ID/API version/branch in `NOTES.md`, distinguishing log evidence from the user's visual screen report.
- [ ] Use the observed AFE/ASM/ADM versions and Linux Beryllium routing to select a separately reviewed minimal audio-path step.
