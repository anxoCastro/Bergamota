#!/usr/bin/env python3
"""Host-side checks for the bounded TAS firmware/calibration parser."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PARSER = ROOT / "tools/beryllium_tas_firmware.h"


class BerylliumTasFirmwareParserTests(unittest.TestCase):
    def test_parser_exports_bounded_firmware_and_calibration_entrypoints(self):
        self.assertTrue(PARSER.is_file(), "bounded TAS parser helper is not implemented")
        source = PARSER.read_text()
        self.assertIn("beryllium_tas_parse_firmware", source)
        self.assertIn("beryllium_tas_parse_calibration", source)
        self.assertIn("BERYLLIUM_TAS_MAX_INPUT", source)

    def test_synthetic_bounds_and_profile_fixtures(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "tas-parser-test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(ROOT / "tools/tests/test_beryllium_tas_firmware.c"),
                 "-o", str(binary)],
                check=True, capture_output=True, text=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
