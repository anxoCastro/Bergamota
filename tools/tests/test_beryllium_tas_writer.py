#!/usr/bin/env python3
"""Host-side tests for TAS paged-register writes."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WRITER = ROOT / "tools/beryllium_tas_writer.h"


class BerylliumTasWriterTests(unittest.TestCase):
    def test_writer_helper_exists(self):
        self.assertTrue(WRITER.is_file(), "TAS register writer is not implemented")

    def test_paged_register_writes(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "tas-writer-test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(ROOT / "tools/tests/test_beryllium_tas_writer.c"),
                 "-o", str(binary)],
                check=True, capture_output=True, text=True,
            )
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
