#!/usr/bin/env python3
"""Host-side contract for local TAS bytes passed through the boot ADT."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import adt  # noqa: E402
import mkpayload  # noqa: E402


def base_adt():
    root = adt.Node()
    root.props = [("name", 0, b"/\0")]
    chosen = adt.Node()
    chosen.props = [("name", 0, b"chosen\0")]
    root.children = [chosen]
    return adt.serialize(root)


class BerylliumTasPayloadTests(unittest.TestCase):
    def test_payload_helper_exists(self):
        source = (ROOT / "tools/mkpayload.py").read_text()
        self.assertIn("def add_tas_firmware_to_adt", source)

    def test_pair_is_inserted_and_round_trips_exact_bytes(self):
        fw, cal = b"tas-firmware\x00\xff", b"phone-calibration\x01"
        raw = mkpayload.add_tas_firmware_to_adt(base_adt(), fw, cal)
        root, end = adt.parse(raw)
        chosen = root.children[0]
        self.assertEqual(end, len(raw))
        self.assertEqual(chosen.prop("sweet-tas-firmware"), fw)
        self.assertEqual(chosen.prop("sweet-tas-calibration"), cal)
        self.assertEqual(mkpayload.add_tas_firmware_to_adt(raw, None, None), raw)

    def test_inputs_must_be_paired_and_bounded(self):
        raw = base_adt()
        with self.assertRaises(ValueError):
            mkpayload.add_tas_firmware_to_adt(raw, b"firmware", None)
        with self.assertRaises(ValueError):
            mkpayload.add_tas_firmware_to_adt(raw, b"x" * (128 * 1024 + 1), b"cal")
        with self.assertRaises(ValueError):
            mkpayload.add_tas_firmware_to_adt(raw, b"fw", b"x" * (128 * 1024 + 1))

    def test_existing_property_names_are_rejected(self):
        root, _ = adt.parse(base_adt())
        root.children[0].props.append(("sweet-tas-firmware", 0, b"other"))
        with self.assertRaises(ValueError):
            mkpayload.add_tas_firmware_to_adt(adt.serialize(root), b"fw", b"cal")

    def test_cli_rejects_a_single_input_file(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/mkpayload.py"), "k", "a", "o",
             "--tas-firmware", "firmware.bin"],
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must be provided together", result.stderr)


if __name__ == "__main__":
    unittest.main()
