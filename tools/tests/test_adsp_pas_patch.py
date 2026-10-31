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
    match = re.search(rf"(?m)^[+ ]{re.escape(name)}\([^\n]*\)$", section)
    if not match:
        raise AssertionError(f"function {name} not found in patch")
    body_start = re.search(r"\n[+ ]\{", section[match.end() :])
    if body_start is None:
        raise AssertionError(f"body for {name} not found in patch")
    depth = 0
    body = []
    start = match.end() + body_start.start() + 1
    for line in section[start:].splitlines():
        if line.startswith(("+", " ")):
            code = line[1:]
        elif line.startswith("-") or line.startswith(("@@", "diff --git ")):
            continue
        else:
            continue
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
        self.assertRegex(self.driver, r"#define ADSP_PAS_ID\s+1u")
        self.assertIn('adsp_pas_call("INIT_IMAGE"', self.driver)
        self.assertIn('adsp_pas_call("MEM_SETUP"', self.driver)
        self.assertIn('adsp_pas_call("AUTH_AND_RESET"', self.driver)
        self.assertNotIn('adsp_pas_call("GLINK"', self.driver)
        self.assertNotIn('adsp_pas_call("SHUTDOWN"', self.driver)
        self.assertIn("watchdog SPI 162 is not observed", self.driver)
        self.assertNotIn("ADSP_SMP2P_OUT_ITEM", self.driver)
        self.assertNotRegex(self.driver, r"adsp_smem\s*\[[^]]+\]\s*=")

    def test_default_and_mode_one_do_not_reach_pas_start(self):
        probe = added_function_body(self.driver, "beryllium_adsp_probe")
        firmware_probe = added_function_body(self.driver, "beryllium_adsp_firmware_probe")
        self.assertIn("mode != 1 && mode != 2", probe)
        self.assertIn("adsp_pas_requested = mode == 2", probe)
        self.assertNotIn("beryllium_adsp_pas_start", probe)
        self.assertIn("if (adsp_pas_requested)", firmware_probe)
        self.assertLess(firmware_probe.index("if (adsp_pas_requested)"),
                        firmware_probe.index("beryllium_adsp_pas_start()"))

    def test_full_bundle_loader_rejects_short_split_segments(self):
        loader = added_function_body(self.driver, "adsp_fw_read_full_bundle")
        self.assertIn("seg_size != filesz", loader)
        self.assertIn("adsp_fw_read_file", loader)
        start = added_function_body(self.driver, "beryllium_adsp_pas_start")
        self.assertLess(start.index("adsp_fw_read_full_bundle"),
                        start.index('adsp_pas_call("INIT_IMAGE"'))

    def test_adsp_qmp_wrapper_reuses_existing_open_channel(self):
        wlan = diff_section(self.patch, "bsd/dev/beryllium_wlan.c")
        qmp = added_function_body(wlan, "beryllium_qmp_set_adsp_load_state")
        self.assertIn("if (!ms.qmp_open && !ms_qmp_open())", qmp)
        self.assertIn("name: adsp, val:", qmp)

    def test_xo_clock_helper_votes_exact_verified_on_value(self):
        rpmh = diff_section(self.patch, "bsd/dev/sweet_rpmh.c")
        xo = added_function_body(rpmh, "sweet_rpmh_xo_enable")
        self.assertIn('rh_lookup_slave("xo.lvl", RH_ARC', xo)
        self.assertIn("const uint32_t value = 3", xo)

    def test_scm_interrupted_call_reuses_the_returned_x6_token(self):
        call = added_function_body(self.driver, "adsp_scm_call")
        self.assertIn("call.a[0] = SCM_INTERRUPTED", call)
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
