#!/usr/bin/env python3
"""Static safety checks for the opt-in Beryllium ADSP PAS stage."""

from pathlib import Path
import argparse
import re
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "xnu/patches-12377/1132-beryllium-adsp-pas-startup.patch"
sys.path.insert(0, str(ROOT / "tools"))
import adt  # noqa: E402
import mkpayload  # noqa: E402


def diff_section(patch: str, path: str) -> str:
    marker = f"diff --git a/{path} b/{path}"
    start = patch.find(marker)
    if start < 0:
        return ""
    end = patch.find("\ndiff --git ", start + len(marker))
    return patch[start:] if end < 0 else patch[start:end]


def added_function_body(section: str, name: str) -> str:
    match = re.search(rf"(?m)^\+{re.escape(name)}\([^\n]*\)$", section)
    if not match:
        raise AssertionError(f"added function {name} not found in patch")
    body_start = section.find("\n+{", match.end())
    if body_start < 0:
        raise AssertionError(f"body for {name} not found in patch")
    depth = 0
    body = []
    for line in section[body_start + 1 :].splitlines():
        if not line.startswith("+"):
            continue
        code = line[1:]
        depth += code.count("{") - code.count("}")
        body.append(code)
        if depth == 0:
            return "\n".join(body)
    raise AssertionError(f"body for {name} is not closed in patch")


class AdspPasPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = PATCH.read_text() if PATCH.exists() else ""
        cls.driver = diff_section(cls.patch, "bsd/dev/beryllium_adsp.c")

    def test_pas_start_has_an_explicit_mode_two_gate(self):
        probe = added_function_body(self.driver, "beryllium_adsp_probe")
        self.assertIn('PE_parse_boot_argn("sweetadsp", &mode, sizeof(mode))', probe)
        self.assertIn("mode != 1 && mode != 2", probe)
        self.assertIn("adsp_pas_requested = mode == 2", probe)

    def test_ready_or_fatal_snapshot_aborts_before_mutating_pas(self):
        start = added_function_body(self.driver, "beryllium_adsp_pas_start")
        self.assertLess(start.index("adsp_remote_ready"), start.index('adsp_pas_call("INIT_IMAGE"'))
        self.assertLess(start.index("adsp_remote_fatal"), start.index('adsp_pas_call("INIT_IMAGE"'))
        self.assertIn("physical state unknown", start)

    def test_all_firmware_and_carveout_gates_precede_pas_mutation(self):
        start = added_function_body(self.driver, "beryllium_adsp_pas_start")
        init = start.index('adsp_pas_call("INIT_IMAGE"')
        for gate in (
            "adsp_fw_read_full_bundle",
            "adsp_check_carveout_marker",
            "adsp_scm_pas_supported",
            "beryllium_qmp_set_adsp_load_state",
            "sweet_rpmh_xo_enable",
        ):
            self.assertLess(start.index(gate), init, gate)

    def test_pas_sequence_is_scoped_to_id_one_and_stops_before_glink(self):
        self.assertIn("#define ADSP_PAS_ID           1u", self.driver)
        self.assertIn('adsp_pas_call("INIT_IMAGE"', self.driver)
        self.assertIn('adsp_pas_call("MEM_SETUP"', self.driver)
        self.assertIn('adsp_pas_call("AUTH_AND_RESET"', self.driver)
        self.assertNotIn('adsp_pas_call("GLINK"', self.driver)
        self.assertNotIn("ADSP_SMP2P_OUT_ITEM", self.driver)
        self.assertNotRegex(self.driver, r"adsp_smem\s*\[[^]]+\]\s*=")

    def test_scm_interrupted_call_reuses_the_returned_x6_token(self):
        call = added_function_body(self.driver, "adsp_scm_call")
        self.assertIn("call.a[0] = 1", call)
        self.assertIn("call.a[6] = out->a[6]", call)
        self.assertIn("SCM_RESUME_LIMIT", call)

    def test_payload_builder_marks_only_the_verified_adsp_range_when_requested(self):
        builder = (ROOT / "tools/mkpayload.py").read_text()
        self.assertIn("def add_adsp_region_to_adt", builder)
        self.assertIn("--adsp-region", builder)
        self.assertIn("sweet-adsp-region", builder)
        self.assertIn("0x8c500000", builder)
        self.assertIn("0x01e00000", builder)

    def test_payload_marker_is_opt_in_and_round_trips_as_two_u64_values(self):
        root = adt.Node()
        root.props = [('name', 0, b'/\0')]
        chosen = adt.Node()
        chosen.props = [('name', 0, b'chosen\0')]
        root.children = [chosen]
        original = adt.serialize(root)

        marked = mkpayload.add_adsp_region_to_adt(original, 0x8c500000, 0x01e00000)
        parsed, _ = adt.parse(marked)
        chosen_out = next(node for node in parsed.children if node.name == 'chosen')
        self.assertEqual(struct.unpack('<QQ', chosen_out.prop('sweet-adsp-region')),
                         (0x8c500000, 0x01e00000))
        self.assertEqual(mkpayload.add_adsp_region_to_adt(original, None, None), original)

    def test_payload_builder_rejects_unverified_adsp_ranges(self):
        root = adt.Node()
        root.props = [('name', 0, b'/\0')]
        chosen = adt.Node()
        chosen.props = [('name', 0, b'chosen\0')]
        root.children = [chosen]
        with self.assertRaises(ValueError):
            mkpayload.add_adsp_region_to_adt(adt.serialize(root), 0x8c500000, 0x200000)
        with self.assertRaises(argparse.ArgumentTypeError):
            mkpayload.parse_adsp_region('0x8c500000:0x200000')

    def test_payload_builder_accepts_only_the_verified_cli_range(self):
        self.assertEqual(mkpayload.parse_adsp_region('0x8c500000:0x01e00000'),
                         (0x8c500000, 0x01e00000))


if __name__ == "__main__":
    unittest.main()
