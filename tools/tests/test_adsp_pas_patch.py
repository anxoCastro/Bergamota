#!/usr/bin/env python3
"""Static safety checks for the opt-in Beryllium ADSP PAS stage."""

from pathlib import Path
import argparse
import re
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "xnu/patches-12377/1132-beryllium-adsp-pas-startup.patch"
PREFLIGHT_PATCH = ROOT / "xnu/patches-12377/1131-beryllium-adsp-apr.patch"
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
    match = re.search(rf"(?m)^[+ ]?{re.escape(name)}\([^\n]*\)$", section)
    if not match:
        raise AssertionError(f"function {name} not found in patch")
    body_start = re.search(r"\n[+ ]?\{", section[match.end() :])
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
            code = line
        depth += code.count("{") - code.count("}")
        body.append(code)
        if depth == 0:
            return "\n".join(body)
    raise AssertionError(f"body for {name} is not closed in patch")


class AdspPasPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = PATCH.read_text() if PATCH.exists() else ""
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", "--include=bsd/dev/beryllium_adsp.c",
                            str(PREFLIGHT_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", "--include=bsd/dev/beryllium_adsp.c",
                            str(PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            cls.driver = (work / "bsd/dev/beryllium_adsp.c").read_text()
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=work,
                           check=True, capture_output=True, text=True)
            for later in sorted(PATCH.parent.glob("*.patch")):
                try:
                    number = int(later.name.split("-", 1)[0])
                except ValueError:
                    continue
                if not 1131 <= number <= 1146:
                    continue
                if "bsd/dev/beryllium_adsp.c" not in later.read_text(errors="replace"):
                    continue
                subprocess.run(["git", "apply", "--include=bsd/dev/beryllium_adsp.c",
                                str(later)], cwd=work,
                               check=True, capture_output=True, text=True)
            cls.latest_driver = (work / "bsd/dev/beryllium_adsp.c").read_text()

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
            "adsp_scm_pas_prerequisites",
            "beryllium_qmp_set_adsp_load_state",
            "sweet_rpmh_xo_enable",
        ):
            self.assertLess(start.index(gate), init, gate)

    def test_adsp_carveout_uses_unmappable_window_before_unmap(self):
        start = added_function_body(self.driver, "beryllium_adsp_pas_start")
        normalized = " ".join(start.split())
        self.assertIn("ml_io_map_unmappable((vm_offset_t)ADSP_PAS_REGION_PA, ADSP_PAS_REGION_SIZE, VM_WIMG_WCOMB)", normalized)
        self.assertNotIn("ml_io_map_wcomb(", start)
        self.assertIn("ml_io_unmap(region, ADSP_PAS_REGION_SIZE)", start)

    def test_pas_support_query_is_advisory_and_references_are_read_only(self):
        supported = added_function_body(self.driver, "adsp_scm_pas_prerequisites")
        query = added_function_body(self.driver, "adsp_scm_pas_id_supported")
        self.assertIn('adsp_scm_pas_id_supported("ADSP", ADSP_PAS_ID)', supported)
        self.assertIn('adsp_scm_pas_id_supported("MPSS reference", 4u)', supported)
        self.assertIn('adsp_scm_pas_id_supported("GPU reference", 13u)', supported)
        self.assertNotIn('{ "IS_SUPPORTED", 7 }', supported)
        diagnostic = supported[supported.index('if (!adsp_scm_pas_id_supported("ADSP"'):]
        self.assertNotIn("return false", diagnostic)
        self.assertIn("IS_SUPPORTED is advisory", diagnostic)
        self.assertIn("SCM_SIP | 0x0200u | 7u", query)
        self.assertIn("x0=%#llx x1=%#llx x2=%#llx x3=%#llx", query)
        self.assertIn("(unsigned long long)out.a[3]", query)
        self.assertNotIn("adsp_pas_call", query)
        self.assertNotIn('adsp_pas_call("INIT_IMAGE"', supported)

    def test_pas_sequence_is_scoped_to_id_one_and_stops_before_glink(self):
        self.assertRegex(self.driver, r"#define ADSP_PAS_ID\s+1u")
        self.assertIn('adsp_pas_call("INIT_IMAGE"', self.driver)
        self.assertIn('adsp_pas_call("MEM_SETUP"', self.driver)
        self.assertIn('adsp_pas_call("AUTH_AND_RESET"', self.driver)
        self.assertNotIn('adsp_pas_call("GLINK"', self.driver)
        self.assertNotIn('adsp_pas_call("SHUTDOWN"', self.driver)
        self.assertIn("watchdog SPI 162 is not observed", self.driver)

    def test_pas_prepare_publishes_adsp_outbound_smp2p_before_init_image(self):
        start = added_function_body(self.driver, "beryllium_adsp_pas_start")
        prepare = added_function_body(self.driver, "adsp_smp2p_out_setup")
        map_ipc = added_function_body(self.driver, "adsp_smp2p_map_ipc")
        kick = added_function_body(self.driver, "adsp_smp2p_kick")
        self.assertRegex(self.driver, r"#define ADSP_SMP2P_OUT_ITEM\s+429u")
        self.assertRegex(self.driver, r"#define ADSP_SMP2P_DOORBELL\s+10u")
        self.assertLess(start.index("adsp_smp2p_prepare"), start.index('adsp_pas_call("INIT_IMAGE"'))
        self.assertLess(prepare.index("adsp_smp2p_map_ipc"), prepare.index("adsp_smem_alloc_private_item"))
        self.assertIn("ADSP_APSS_SHARED_PA", map_ipc)
        self.assertIn("adsp_smp2p_ipc + 12", kick)
        self.assertIn("1u << ADSP_SMP2P_DOORBELL", kick)
        for item in (
            "ADSP_SMP2P_OUT_ITEM",
            "ADSP_SMP2P_ITEM_SIZE",
            "ADSP_LOCAL_PID",
            "ADSP_REMOTE_PID",
            '"stop"',
            "ADSP_SMP2P_MAGIC",
        ):
            self.assertIn(item, prepare, item)
        self.assertIn("adsp_smem_alloc_private_item", self.driver)
        self.assertNotIn("ADSP_SMP2P_IN_ITEM", prepare)
        self.assertNotRegex(prepare, r"adsp_smem\s*\[[^]]+\]\s*=")

    def test_outbound_smp2p_readback_is_passive_and_brackets_pas_start(self):
        snapshot = added_function_body(self.latest_driver, "adsp_smp2p_out_snapshot")
        start = added_function_body(self.latest_driver, "beryllium_adsp_pas_start")
        self.assertIn("ADSP_SMP2P_OUT_ITEM", snapshot)
        self.assertIn("ADSP_LOCAL_PID", snapshot)
        self.assertIn("ADSP_REMOTE_PID", snapshot)
        self.assertIn('strcmp(name, "master-kernel")', snapshot)
        self.assertIn("ch == 0 ? '\\0'", snapshot)
        self.assertIn("adsp_smem_rd32", snapshot)
        self.assertNotIn("adsp_smem_wr32", snapshot)
        self.assertNotIn("adsp_smp2p_kick", snapshot)
        self.assertLess(start.index("adsp_smp2p_prepare"),
                        start.index('adsp_smp2p_out_snapshot("after publish")'))
        self.assertLess(start.index('adsp_smp2p_out_snapshot("after publish")'),
                        start.index('adsp_pas_call("INIT_IMAGE"'))
        self.assertLess(start.index('adsp_pas_call("AUTH_AND_RESET"'),
                        start.index('adsp_smp2p_out_snapshot("after PAS wait")'))

    def test_pas_refuses_malformed_or_active_smp2p_lifecycle_state(self):
        start = added_function_body(self.driver, "beryllium_adsp_pas_start")
        prepare = start.index("adsp_smp2p_prepare")
        for gate in (
            "adsp_smp2p_ambiguous",
            "adsp_state == BERYLLIUM_ADSP_FAILED",
            "adsp_remote_ready",
            "adsp_remote_fatal",
            "adsp_remote_handover",
            "adsp_remote_stop_ack",
        ):
            self.assertLess(start.index(gate), prepare, gate)
        probe = added_function_body(self.driver, "adsp_smp2p_readonly_probe")
        self.assertIn("adsp_smp2p_ambiguous = true", probe)
        self.assertIn("adsp_remote_handover =", probe)
        self.assertIn("adsp_remote_stop_ack =", probe)

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

    def test_loaded_segments_are_verified_before_pas_authentication(self):
        start = added_function_body(self.latest_driver, "beryllium_adsp_pas_start")
        verifier = added_function_body(self.latest_driver, "adsp_verify_loaded_segments")
        self.assertLess(start.index("adsp_verify_loaded_segments"),
                        start.index('adsp_pas_call("AUTH_AND_RESET"'))
        self.assertIn("if (!adsp_verify_loaded_segments", start)
        self.assertIn("PAS AUTH_AND_RESET not attempted", start)
        self.assertIn("seg->filesz", verifier)
        self.assertIn("seg->memsz", verifier)
        self.assertIn("region[off + j] != seg->data[j]", verifier)
        self.assertIn("region[off + j] != 0", verifier)
        self.assertIn("readback mismatch", verifier)

    def test_pas_metadata_uses_xnu_managed_contiguous_memory(self):
        allocator = added_function_body(self.latest_driver, "adsp_metadata_alloc_contig")
        self.assertIn("kmem_alloc_contig(kernel_map", allocator)
        self.assertIn("pmap_find_phys", allocator)
        self.assertIn("pmap_set_cache_attributes", allocator)
        self.assertIn("sweet_dma_sync_va", allocator)
        self.assertIn("VM_WIMG_WCOMB", allocator)
        self.assertNotIn("SecureDTGetProperty", allocator)
        self.assertNotIn("ADSP_PAS_METADATA_PA", self.latest_driver)

    def test_permanent_pas_metadata_is_not_unmapped(self):
        start = added_function_body(self.latest_driver, "beryllium_adsp_pas_start")
        self.assertIn("KMA_PERMANENT", added_function_body(self.latest_driver,
                                                           "adsp_metadata_alloc_contig"))
        self.assertNotIn("ml_io_unmap(metadata_kva", start)
        self.assertIn("ml_io_unmap(region, ADSP_PAS_REGION_SIZE)", start)

    def test_payload_metadata_region_marker_is_exact_and_conflict_checked(self):
        root = adt.Node()
        root.props.append(("name", 0, b"device-tree\0"))
        chosen = adt.Node()
        chosen.props.append(("name", 0, b"chosen\0"))
        root.children.append(chosen)
        raw = adt.serialize(root)

        marked = mkpayload.add_adsp_metadata_region_to_adt(raw, 0xc0000000, 0x4000)
        parsed, _ = adt.parse(marked)
        chosen = next(node for node in parsed.children if node.name == "chosen")
        self.assertEqual(struct.unpack("<2Q", chosen.prop("sweet-adsp-metadata-region")),
                         (0xc0000000, 0x4000))
        with self.assertRaises(ValueError):
            mkpayload.add_adsp_metadata_region_to_adt(raw, 0xc0004000, 0x4000)

        chosen.props = [
            (name, flags, struct.pack("<2Q", 0xc0004000, 0x4000)
             if name == "sweet-adsp-metadata-region" else value)
            for name, flags, value in chosen.props
        ]
        with self.assertRaises(ValueError):
            mkpayload.add_adsp_metadata_region_to_adt(adt.serialize(parsed), 0xc0000000, 0x4000)

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

    def test_startup_patch_applies_after_preflight_patch(self):
        self.assertIn("adsp_smp2p_readonly_probe(bool quiet)", self.driver)
        self.assertIn("beryllium_adsp_pas_start(void)", self.driver)
        targets = (
            "bsd/dev/beryllium_adsp.c",
            "bsd/dev/beryllium_wlan.c",
            "bsd/dev/sweet_rpmh.c",
        )
        patch_dir = PATCH.parent
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=work,
                           check=True, capture_output=True, text=True)
            for target in targets:
                for earlier in sorted(patch_dir.glob("*.patch")):
                    try:
                        earlier_number = int(earlier.name.split("-", 1)[0])
                        target_number = int(PATCH.name.split("-", 1)[0])
                    except ValueError:
                        continue
                    if earlier_number >= target_number:
                        continue
                    marker = f"diff --git a/{target} b/{target}"
                    if marker not in earlier.read_text(errors="replace"):
                        continue
                    subprocess.run(["git", "apply", f"--include={target}", str(earlier)],
                                   cwd=work, check=True, capture_output=True, text=True)
                subprocess.run(["git", "apply", "--check", f"--include={target}", str(PATCH)],
                               cwd=work, check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
