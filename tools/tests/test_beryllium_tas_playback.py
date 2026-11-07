#!/usr/bin/env python3
"""Patch integration checks for TAS parser mode 9."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "xnu/patches-12377/1190-beryllium-tas-finite-playback.patch"
PARSER = ROOT / "tools/beryllium_tas_firmware.h"


class BerylliumTasPlaybackPatchTests(unittest.TestCase):
    def test_mode_nine_patch_embeds_the_validated_parser_byte_for_byte(self):
        self.assertTrue(PATCH.is_file(), "mode 9 patch is not implemented")
        patch = PATCH.read_text()
        marker = "diff --git a/bsd/dev/beryllium_tas_firmware.h b/bsd/dev/beryllium_tas_firmware.h\n"
        self.assertIn(marker, patch)
        section = patch.split(marker, 1)[1].split("\ndiff --git ", 1)[0]
        added = "\n".join(line[1:] for line in section.splitlines()
                            if line.startswith("+") and not line.startswith("+++"))
        self.assertEqual(added + "\n", PARSER.read_text())
        self.assertIn('PE_parse_boot_argn("sweetadsp", &mode, sizeof(mode))', patch)
        self.assertIn("mode == 9", patch)
        self.assertIn("sweet-tas-firmware", patch)
        self.assertIn("sweet-tas-calibration", patch)

    def test_patch_replays_after_the_adsp_chain_and_matches_its_staged_source(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            subprocess.run(["git", "init", "-q"], cwd=work, check=True,
                           capture_output=True, text=True)
            includes = ("bsd/dev/beryllium_adsp.c",
                        "bsd/dev/beryllium_tas_id_probe.h",
                        "bsd/dev/beryllium_tas_firmware.h")
            patches = sorted((ROOT / "xnu/patches-12377").glob("*.patch"))
            for patch in patches:
                try:
                    number = int(patch.name.split("-", 1)[0])
                except ValueError:
                    continue
                if not 1131 <= number <= 1190:
                    continue
                command = ["git", "apply"]
                command.extend(f"--include={name}" for name in includes)
                command.append(str(patch))
                subprocess.run(command, cwd=work, check=True,
                               capture_output=True, text=True)
            staged = (ROOT / ".superpowers/sdd/2026-11-06-beryllium-tas-id-probe/source/bsd/dev")
            self.assertEqual((work / includes[0]).read_bytes(),
                             (staged / "beryllium_adsp.c").read_bytes())
            self.assertEqual((work / includes[2]).read_bytes(),
                             (staged / "beryllium_tas_firmware.h").read_bytes())

    def test_mode_nine_parser_preflight_stops_before_hardware_or_adsp_start(self):
        patch = PATCH.read_text()
        driver = patch.split("diff --git a/bsd/dev/beryllium_adsp.c", 1)[1]
        self.assertIn("adsp_tas_validate_payload_properties", driver)
        preflight = driver.split("adsp_tas_validate_payload_properties(void)", 1)[1]
        preflight = preflight.split("\n+static bool", 1)[0]
        for forbidden in ("adsp_tas_reset(", "ml_io_map(", "adsp_audio_gpio_write(",
                          "adsp_apr", "adsp_pas_call("):
            self.assertNotIn(forbidden, preflight)
        branch = driver.split("if (mode == 9)", 1)[1].split("if (mode == 8)", 1)[0]
        self.assertIn("adsp_tas_validate_payload_properties", branch)
        self.assertIn("return;", branch)


if __name__ == "__main__":
    unittest.main()
