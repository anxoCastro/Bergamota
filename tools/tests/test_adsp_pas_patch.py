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
NEGOTIATION_PATCH = ROOT / "xnu/patches-12377/1147-beryllium-adsp-smp2p-version-negotiation.patch"
GLINK_PATCH = ROOT / "xnu/patches-12377/1148-beryllium-adsp-glink-readonly-preflight.patch"
GLINK_MODE3_PATCH = ROOT / "xnu/patches-12377/1149-beryllium-adsp-glink-mode3.patch"
APR_MODE4_PATCH = ROOT / "xnu/patches-12377/1150-beryllium-adsp-apr-q6core-mode4.patch"
APR_OPEN_DIAGNOSTICS_PATCH = ROOT / "xnu/patches-12377/1151-beryllium-adsp-apr-open-diagnostics.patch"
FASTRPC_OPEN_PATCH = ROOT / "xnu/patches-12377/1152-beryllium-adsp-fastrpc-peer-open.patch"
IPCRTR_OPEN_PATCH = ROOT / "xnu/patches-12377/1153-beryllium-adsp-ipcrtr-peer-open.patch"
GLINK_SSR_OPEN_PATCH = ROOT / "xnu/patches-12377/1154-beryllium-adsp-glink-ssr-peer-open.patch"
LOOPBACK_CTL_LPASS_OPEN_PATCH = ROOT / "xnu/patches-12377/1155-beryllium-adsp-loopback-ctl-lpass-peer-open.patch"
APR_APPS2_OPEN_PATCH = ROOT / "xnu/patches-12377/1156-beryllium-adsp-apr-apps2-peer-open.patch"
APR_INTENT_ID_ZERO_PATCH = ROOT / "xnu/patches-12377/1158-beryllium-adsp-apr-intent-id-zero.patch"
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
    match = re.search(rf"(?m)^[+ ]?{re.escape(name)}\((?:[^)]|\n)*?\)$", section)
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
                if not 1131 <= number <= 1158:
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

    def test_smp2p_version_negotiation_is_wired_to_ap_item(self):
        negotiate = added_function_body(self.latest_driver, "adsp_smp2p_negotiate_remote")
        start = added_function_body(self.latest_driver, "beryllium_adsp_pas_start")
        setup = added_function_body(self.latest_driver, "adsp_smp2p_out_setup")
        self.assertIn("beryllium_smp2p_negotiate_header", negotiate)
        self.assertIn("ADSP_SMP2P_OUT_ITEM", setup)
        self.assertIn("adsp_smp2p_out_data_off = data_off", setup)
        self.assertNotIn("ADSP_SMP2P_IN_ITEM", negotiate)
        self.assertIn("BERYLLIUM_SMP2P_DOWNGRADE", negotiate)
        downgrade = negotiate.index("BERYLLIUM_SMP2P_DOWNGRADE")
        kick = negotiate.index("adsp_smp2p_kick()", downgrade)
        equal = negotiate.index("BERYLLIUM_SMP2P_FEATURES_CHANGED")
        self.assertLess(downgrade, kick)
        self.assertLess(kick, equal)
        self.assertNotIn("adsp_smp2p_kick()", negotiate[equal:])
        writes = re.findall(r"adsp_smem_wr(?:8|16|32)\(([^,]+)", negotiate)
        self.assertEqual(writes, ["adsp_smp2p_out_data_off + 4"] * 2)
        self.assertNotIn("ADSP_SMP2P_IN_ITEM", negotiate)
        self.assertLess(start.index("adsp_smp2p_readonly_probe(true)"),
                        start.index("adsp_smp2p_negotiate_remote()"))

    def test_compiled_smp2p_header_transitions(self):
        header_path = "bsd/dev/beryllium_smp2p_negotiation.h"
        harness = ROOT / "tools/tests/test_adsp_smp2p_negotiation.c"
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(NEGOTIATION_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            executable = work / "test-smp2p-negotiation"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str((work / header_path).parent), str(harness),
                "-o", str(executable),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], check=False,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "SMP2P negotiation transitions passed")

    def test_compiled_glink_survey_helpers(self):
        header_path = "bsd/dev/beryllium_adsp_glink_survey.h"
        harness = ROOT / "tools/tests/test_adsp_glink_survey.c"
        self.assertTrue(GLINK_PATCH.is_file(), "patch 1148 must provide the production helper header")
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(GLINK_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            executable = work / "test-glink-survey"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str((work / header_path).parent), str(harness),
                "-o", str(executable),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], check=False,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "GLINK survey helper checks passed")

    def test_compiled_glink_mode3_helpers(self):
        header_path = "bsd/dev/beryllium_adsp_glink_start.h"
        harness = ROOT / "tools/tests/test_adsp_glink_mode3.c"
        self.assertTrue(GLINK_MODE3_PATCH.is_file(),
                        "patch 1149 must provide the production GLINK-start helper header")
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", "--include=bsd/dev/beryllium_adsp_glink_survey.h",
                            str(GLINK_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(GLINK_MODE3_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            executable = work / "test-glink-mode3"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str((work / header_path).parent), str(harness),
                "-o", str(executable),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], check=False,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "GLINK mode-3 helper checks passed")

    def test_mode3_glink_is_gated_by_ready_and_owns_only_ap_fields(self):
        self.assertTrue(GLINK_MODE3_PATCH.is_file(), "patch 1149 is missing")
        probe = added_function_body(self.latest_driver, "beryllium_adsp_probe")
        start = added_function_body(self.latest_driver, "beryllium_adsp_pas_start")
        glink = added_function_body(self.latest_driver, "adsp_glink_mode3_start")
        prepare = added_function_body(self.latest_driver, "adsp_glink_tx_fifo_prepare")
        allocate = added_function_body(self.latest_driver, "adsp_glink_allocate_tx_fifo_locked")
        send_version = added_function_body(self.latest_driver, "adsp_glink_send_version")
        transmit = added_function_body(self.latest_driver, "adsp_glink_send_packet")
        kick = added_function_body(self.latest_driver, "adsp_glink_kick")
        receive = added_function_body(self.latest_driver, "adsp_glink_poll_version")
        consume = added_function_body(self.latest_driver, "adsp_glink_rx_consume")
        readonly = added_function_body(self.latest_driver, "adsp_glink_readonly_survey")

        self.assertIn("beryllium_adsp_glink_mode_valid(mode)", probe)
        self.assertIn("beryllium_adsp_glink_mode_requests_pas(mode)", probe)
        self.assertIn("beryllium_adsp_glink_mode_requests_glink(mode)", probe)
        self.assertNotIn("adsp_smem_wr", readonly)
        self.assertNotIn("adsp_smem_alloc_private_item", readonly)
        self.assertNotIn("adsp_glink_kick", readonly)
        self.assertLess(start.index("Q6V5 ready observed"),
                        start.index("adsp_glink_mode3_start()"))
        self.assertLess(glink.index("adsp_glink_cold_preflight"),
                        glink.index("adsp_glink_tx_fifo_prepare"))
        self.assertLess(glink.index("adsp_glink_tx_fifo_prepare"),
                        glink.index("adsp_glink_cold_indices_after_allocation"))
        self.assertLess(glink.index("adsp_glink_cold_indices_after_allocation"),
                        glink.index("adsp_glink_send_version"))
        self.assertIn("adsp_smem_lock()", prepare)
        self.assertIn("adsp_smem_unlock()", prepare)
        self.assertLess(prepare.index("adsp_smem_lock()"),
                        prepare.index("adsp_smem_private_item("))
        self.assertIn("ADSP_GLINK_TX_FIFO_ITEM", allocate)
        self.assertIn("free_cached - free_uncached", allocate)
        self.assertIn('"dmb sy"', allocate)
        self.assertNotIn("ADSP_GLINK_RX_FIFO_ITEM", allocate)
        self.assertIn("BERYLLIUM_ADSP_GLINK_COMMAND_VERSION_ACK", receive)
        self.assertIn("delay(1000)", receive)
        self.assertIn("ADSP_GLINK_POLL_MS", receive)
        self.assertIn("feature mismatch", receive)
        self.assertIn("adsp_glink_send_packet", send_version)
        self.assertIn("ADSP_GLINK_DOORBELL", kick)
        self.assertNotIn("ADSP_SMP2P_DOORBELL", kick)
        self.assertIn("beryllium_adsp_glink_indices_valid", transmit)
        self.assertIn("cold_start && !beryllium_adsp_glink_cold_indices_valid", transmit)
        self.assertIn("adsp_smem_wr32(items->descriptor.off + 4", transmit)
        self.assertIn("adsp_glink_kick", transmit)
        self.assertIn("adsp_smem_wr32(items->descriptor.off + 8", consume)
        self.assertNotIn("adsp_pas_call", glink)
        self.assertNotRegex(glink + prepare + allocate + transmit + receive + consume,
                            r"adsp_smem_wr32\([^\n]*(?:descriptor_off|descriptor\.off)\s*\+\s*(?:0|12)\b")
        self.assertNotRegex(glink + prepare + allocate + transmit + receive + consume,
                            r"adsp_smem_wr(?:8|16|32)\([^\n]*ADSP_GLINK_RX_FIFO_ITEM")
        self.assertNotIn("ADSP_GLINK_RX_FIFO_ITEM,", prepare)


    def test_smem_lookup_classifies_absent_and_malformed(self):
        lookup = added_function_body(self.latest_driver, "adsp_smem_private_item")
        self.assertIsNotNone(re.search(
            r"enum beryllium_adsp_item_result\s+adsp_smem_private_item\(",
            self.latest_driver), "lookup still has bool return type")
        self.assertIn("return BERYLLIUM_ADSP_ITEM_FOUND", lookup)
        self.assertIn("return BERYLLIUM_ADSP_ITEM_ABSENT", lookup)
        self.assertIn("return BERYLLIUM_ADSP_ITEM_MALFORMED", lookup)
        self.assertIn("ADSP_SMEM_TOC_MAGIC", lookup)
        self.assertIn("ADSP_SMEM_PRIVATE_CANARY", lookup)
        self.assertNotIn("adsp_smem_wr", lookup)
        self.assertNotIn("adsp_smem_alloc_private_item", lookup)
        self.assertNotIn("adsp_smem_lock(", lookup)
        self.assertNotIn("adsp_smem_unlock(", lookup)
        self.assertNotRegex(self.latest_driver, r"!\s*adsp_smem_private_item\(")
        for caller in ("adsp_smp2p_out_snapshot", "adsp_smp2p_readonly_probe",
                       "adsp_crash_reason_read"):
            self.assertIn("!= BERYLLIUM_ADSP_ITEM_FOUND",
                          added_function_body(self.latest_driver, caller), caller)

    def test_compiled_smem_private_item_parser(self):
        header_path = "bsd/dev/beryllium_adsp_glink_survey.h"
        harness = ROOT / "tools/tests/adsp_smem_private_item_harness.c"
        self.assertTrue(GLINK_PATCH.is_file(), "patch 1148 must provide the production helper header")
        body = added_function_body(self.latest_driver, "adsp_smem_private_item")
        function = (
            "static enum beryllium_adsp_item_result\n"
            "adsp_smem_private_item(uint16_t host_a, uint16_t host_b, uint16_t wanted,\n"
            "    uint32_t *data_off, uint32_t *data_size)\n" + body
        )
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(GLINK_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            source = harness.read_text().replace("/* PRODUCTION_FUNCTION */", function)
            source_path = work / "test-adsp-smem-private-item.c"
            source_path.write_text(source)
            executable = work / "test-adsp-smem-private-item"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str((work / header_path).parent), str(source_path),
                "-o", str(executable),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], check=False,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "SMEM private item parser checks passed")

    def test_glink_survey_guards_precede_index_reads(self):
        survey = added_function_body(self.latest_driver, "adsp_glink_readonly_survey")
        read_items = added_function_body(self.latest_driver, "adsp_glink_survey_read_items")
        absent_guard = survey.index(
            "if (first_items.descriptor.result != BERYLLIUM_ADSP_ITEM_FOUND)")
        absent_return = survey.index("return;", absent_guard)
        first_index_read = survey.index("adsp_glink_survey_read_indices", absent_guard)
        self.assertLess(absent_return, first_index_read)

        size_guard = survey.index(
            "if (!beryllium_adsp_glink_descriptor_size_valid(first_items.descriptor.size))")
        size_return = survey.index("return;", size_guard)
        self.assertLess(size_return, first_index_read)

        malformed_guard = read_items.index(
            "items->descriptor.result == BERYLLIUM_ADSP_ITEM_MALFORMED")
        malformed_return = read_items.index("return false;", malformed_guard)
        self.assertLess(malformed_return, read_items.index("return true;", malformed_guard))

    def test_glink_survey_is_read_only_and_after_ready(self):
        survey = added_function_body(self.latest_driver, "adsp_glink_readonly_survey")
        read_items = added_function_body(self.latest_driver, "adsp_glink_survey_read_items")
        log_item = added_function_body(self.latest_driver, "adsp_glink_survey_log_item")
        read_indices = added_function_body(self.latest_driver, "adsp_glink_survey_read_indices")
        log_indices = added_function_body(self.latest_driver, "adsp_glink_survey_log_indices")
        item_equal = added_function_body(self.latest_driver, "adsp_glink_survey_item_equal")
        items_equal = added_function_body(self.latest_driver, "adsp_glink_survey_items_equal")
        log_index = added_function_body(self.latest_driver, "adsp_glink_survey_log_index")
        start = added_function_body(self.latest_driver, "beryllium_adsp_pas_start")
        ready = start[start.index("if (ready) {"):start.index("if (fatal) {")]

        self.assertLess(ready.index("Q6V5 ready observed"),
                        ready.index("adsp_glink_readonly_survey()"))
        self.assertIn("BERYLLIUM_ADSP_ITEM_FOUND", log_item)
        self.assertIn("BERYLLIUM_ADSP_ITEM_ABSENT", log_item)
        self.assertIn("BERYLLIUM_ADSP_ITEM_MALFORMED", log_item)
        self.assertIn("beryllium_adsp_glink_descriptor_size_valid", survey)
        self.assertIn("ADSP_GLINK_DESCRIPTOR_ITEM", read_items)
        self.assertIn("ADSP_GLINK_TX_FIFO_ITEM", read_items)
        self.assertIn("ADSP_GLINK_RX_FIFO_ITEM", read_items)
        self.assertIn("size=%u", log_item)
        self.assertIn("delay(100000)", survey)
        self.assertIn("stable during 100 ms", survey)
        self.assertIn("changed between samples", survey)
        self.assertEqual(read_indices.count("adsp_smem_rd32("), 8)
        self.assertGreaterEqual(read_indices.count('"dmb sy"'), 2)
        self.assertIn("beryllium_adsp_glink_indices_equal", read_indices)
        self.assertIn("beryllium_adsp_glink_indices_equal", survey)
        self.assertIn("adsp_glink_survey_item_equal", items_equal)
        self.assertIn("Q6V5 ready source: item %u entry slave-kernel value=", ready)
        self.assertIn("adsp_remote_ready_entry_value", ready)
        self.assertIn("adsp_remote_ready_entry_value = value", self.latest_driver)

        call_graph = "\n".join((survey, read_items, log_item, read_indices,
                                  log_indices, item_equal, items_equal, log_index,
                                  added_function_body(self.latest_driver,
                                                      "adsp_smem_private_item")))
        for forbidden in (
            "adsp_smem_wr", "adsp_smem_alloc_private_item", "adsp_smem_lock(",
            "adsp_smem_unlock(", "adsp_smp2p_kick", "sweet_gic_spi_",
            "adsp_pas_call", "apr_", "glink_native", "mailbox",
        ):
            self.assertNotIn(forbidden, call_graph, forbidden)

    def test_duplicate_smp2p_ready_entry_cannot_trigger_glink_survey(self):
        probe = added_function_body(self.latest_driver, "adsp_smp2p_readonly_probe")
        duplicate = probe[probe.index("duplicate slave-kernel SMP2P entry"):]
        duplicate = duplicate[:duplicate.index("return;")]
        self.assertIn("adsp_remote_ready = false", duplicate)
        self.assertIn("adsp_remote_ready_entry_valid = false", duplicate)
        self.assertIn("adsp_smp2p_header_valid = false", duplicate)
        self.assertIn("adsp_smp2p_ambiguous = true", duplicate)

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

    def test_rejected_peer_open_logs_bounded_wire_fields(self):
        self.assertTrue(APR_OPEN_DIAGNOSTICS_PATCH.is_file(),
                        "patch 1151 must add diagnostics for the rejected peer OPEN")
        open_body = added_function_body(self.latest_driver, "adsp_apr_open_channel")
        self.assertIn("peer OPEN rejected: command=%u remote-id=%u param2=%#x", open_body)
        self.assertIn("frame_bytes=%lu wire_data=", open_body)
        self.assertIn("dump_bytes > 32", open_body)
        self.assertIn("for (size_t i = 0; i < dump_bytes; i++)", open_body)
        self.assertIn("return false", open_body)

    def test_known_fastrpc_peer_open_is_consumed_without_ack(self):
        open_body = added_function_body(self.latest_driver, "adsp_apr_open_channel")
        self.assertIn("bool ignored_fastrpc_open = false", open_body)
        self.assertIn("beryllium_adsp_apr_fastrpc_open_valid", open_body)
        self.assertIn("ignoring unhandled peer channel fastrpcglink-apps-dsp", open_body)
        self.assertIn("ignored_fastrpc_open = true", open_body)
        fast_open_start = open_body.index("if (!ignored_fastrpc_open")
        fast_open_end = open_body.index("} else if (remote_open", fast_open_start)
        fast_open = open_body[fast_open_start:fast_open_end]
        self.assertIn("!remote_open", fast_open)
        self.assertNotIn("adsp_apr_send_control", fast_open)
        self.assertNotIn("return false", fast_open)
        self.assertIn("adsp_glink_rx_consume_frame(items, tail, frame_bytes)", open_body)
        self.assertLess(open_body.index("adsp_glink_rx_consume_frame(items, tail, frame_bytes)"),
                        open_body.index("if (local_open_ack && remote_open)"))

    def test_known_ipcrtr_peer_open_is_consumed_without_ack(self):
        open_body = added_function_body(self.latest_driver, "adsp_apr_open_channel")
        self.assertIn("bool ignored_ipcrtr_open = false", open_body)
        self.assertIn("beryllium_adsp_apr_ipcrtr_open_valid", open_body)
        self.assertIn("ignoring unhandled peer channel IPCRTR", open_body)
        self.assertIn("ignored_ipcrtr_open = true", open_body)
        ipcrtr_start = open_body.index("if (!ignored_ipcrtr_open")
        ipcrtr_end = open_body.index("} else if (remote_open", ipcrtr_start)
        ipcrtr_open = open_body[ipcrtr_start:ipcrtr_end]
        self.assertIn("!remote_open", ipcrtr_open)
        self.assertNotIn("adsp_apr_send_control", ipcrtr_open)
        self.assertNotIn("return false", ipcrtr_open)

    def test_known_glink_ssr_peer_open_is_consumed_without_ack(self):
        open_body = added_function_body(self.latest_driver, "adsp_apr_open_channel")
        self.assertIn("bool ignored_glink_ssr_open = false", open_body)
        self.assertIn("beryllium_adsp_apr_glink_ssr_open_valid", open_body)
        self.assertIn("ignoring unhandled peer channel glink_ssr", open_body)
        self.assertIn("ignored_glink_ssr_open = true", open_body)
        ssr_start = open_body.index("if (!ignored_glink_ssr_open")
        ssr_end = open_body.index("} else if (remote_open", ssr_start)
        ssr_open = open_body[ssr_start:ssr_end]
        self.assertIn("!remote_open", ssr_open)
        self.assertNotIn("adsp_apr_send_control", ssr_open)
        self.assertNotIn("return false", ssr_open)

    def test_known_loopback_ctl_lpass_peer_open_is_consumed_without_ack(self):
        open_body = added_function_body(self.latest_driver, "adsp_apr_open_channel")
        self.assertIn("bool ignored_loopback_ctl_lpass_open = false", open_body)
        self.assertIn("beryllium_adsp_apr_loopback_ctl_lpass_open_valid", open_body)
        self.assertIn("ignoring unhandled peer channel LOOPBACK_CTL_LPASS", open_body)
        self.assertIn("ignored_loopback_ctl_lpass_open = true", open_body)
        loopback_start = open_body.index("if (!ignored_loopback_ctl_lpass_open")
        loopback_end = open_body.index("} else if (remote_open", loopback_start)
        loopback_open = open_body[loopback_start:loopback_end]
        self.assertIn("!remote_open", loopback_open)
        self.assertNotIn("adsp_apr_send_control", loopback_open)
        self.assertNotIn("return false", loopback_open)

    def test_known_apr_apps2_peer_open_is_consumed_after_apr_audio_svc_opens(self):
        open_body = added_function_body(self.latest_driver, "adsp_apr_open_channel")
        self.assertIn("bool ignored_apr_apps2_open = false", open_body)
        self.assertIn("beryllium_adsp_apr_apps2_open_valid", open_body)
        self.assertIn("ignoring unhandled peer channel apr_apps2", open_body)
        self.assertIn("ignored_apr_apps2_open = true", open_body)
        apps2_start = open_body.index("if (!ignored_apr_apps2_open")
        apps2_end = open_body.index("} else if (remote_open", apps2_start)
        apps2_open = open_body[apps2_start:apps2_end]
        self.assertNotIn("!remote_open", apps2_open)
        self.assertNotIn("adsp_apr_send_control", apps2_open)
        self.assertNotIn("return false", apps2_open)

    def test_remote_intent_rejection_logs_bounded_frame_without_consuming_it(self):
        wait = added_function_body(self.latest_driver, "adsp_apr_wait_remote_intent")
        self.assertIn("#define ADSP_APR_GLINK_FRAME_MAX 528u", self.latest_driver)
        self.assertIn(
            'printf("SWEET ADSP: APR intent frame rejected status=%u command=%u "',
            wait,
        )
        self.assertIn(
            '"param1=%#x param2=%#x frame_bytes=%lu copied=%lu raw_bytes=%lu raw=",',
            wait,
        )
        self.assertIn(
            "size_t dump_bytes = frame_bytes != 0 && frame_bytes <= copy_bytes ?",
            wait,
        )
        self.assertIn("frame_bytes : copy_bytes;", wait)
        self.assertIn("size_t copy_bytes = used < sizeof(frame) ? used : sizeof(frame);",
                      wait)
        self.assertIn("for (size_t i = 0; i < dump_bytes; i++)", wait)
        self.assertIn('printf("%02x", (unsigned)frame[i])', wait)
        reject_start = wait.index("if (status != BERYLLIUM_ADSP_GLINK_FRAME_COMPLETE")
        consume_start = wait.index("if (!adsp_glink_rx_consume_frame")
        rejection = wait[reject_start:consume_start]
        self.assertIn("return false", rejection)
        self.assertNotIn("adsp_glink_rx_consume_frame", rejection)


class AdspAprMode4Tests(unittest.TestCase):
    def test_compiled_apr_mode4_helpers(self):
        self.assertTrue(APR_MODE4_PATCH.is_file(),
                        "patch 1150 must provide the production APR helper header")
        header_path = "bsd/dev/beryllium_adsp_apr_probe.h"
        glink_header_path = "bsd/dev/beryllium_adsp_glink_start.h"
        harness = ROOT / "tools/tests/test_adsp_apr_mode4.c"
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(APR_MODE4_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", "--include=bsd/dev/beryllium_adsp_glink_survey.h",
                            str(GLINK_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", f"--include={glink_header_path}",
                            str(GLINK_MODE3_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", f"--include={glink_header_path}",
                            str(APR_MODE4_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(FASTRPC_OPEN_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(IPCRTR_OPEN_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(GLINK_SSR_OPEN_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            self.assertTrue(LOOPBACK_CTL_LPASS_OPEN_PATCH.is_file(),
                            "patch 1155 must provide the exact LOOPBACK_CTL_LPASS validator")
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(LOOPBACK_CTL_LPASS_OPEN_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            self.assertTrue(APR_APPS2_OPEN_PATCH.is_file(),
                            "patch 1156 must provide the exact apr_apps2 validator")
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(APR_APPS2_OPEN_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            self.assertTrue(APR_INTENT_ID_ZERO_PATCH.is_file(),
                            "patch 1158 must allow valid remote intent ID zero")
            subprocess.run(["git", "apply", f"--include={header_path}",
                            str(APR_INTENT_ID_ZERO_PATCH)], cwd=work,
                           check=True, capture_output=True, text=True)
            executable = work / "test-apr-mode4"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str((work / header_path).parent), str(harness),
                "-o", str(executable),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], check=False,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "APR mode-4 helper checks passed")

    def test_mode4_is_a_single_read_only_q6core_request_after_glink(self):
        self.assertTrue(APR_MODE4_PATCH.is_file(), "patch 1150 is missing")
        patch = APR_MODE4_PATCH.read_text()
        driver = diff_section(patch, "bsd/dev/beryllium_adsp.c")
        apr_header = diff_section(patch, "bsd/dev/beryllium_adsp_apr_probe.h")
        self.assertIn("adsp_apr_requested = mode == 4", driver)
        self.assertIn("adsp_glink_mode3_start()", driver)
        self.assertIn("adsp_apr_mode4_start()", driver)
        self.assertIn('#define BERYLLIUM_ADSP_APR_CHANNEL_NAME "apr_audio_svc"', apr_header)
        self.assertIn("ADSP_APR_CMD_GET_FWK_VERSION", driver)
        self.assertIn("BERYLLIUM_ADSP_APR_INTENT_COUNT", driver)
        self.assertIn("BERYLLIUM_ADSP_APR_INTENT_BYTES", driver)
        self.assertNotRegex(driver, r"q6afe|q6asm|AFE_PORT|ASM_STREAM|codec_start|pas_shutdown")
        self.assertNotIn("RX_INTENT_REQ", driver)
        self.assertNotIn('adsp_pas_call("SHUTDOWN"', driver)
        start_header = diff_section(patch, "bsd/dev/beryllium_adsp_glink_start.h")
        self.assertIn("mode <= 4", start_header)
        apr_start = added_function_body(driver, "adsp_apr_mode4_start")
        ready_path = driver[driver.index("if (adsp_glink_mode3_start())"):]
        self.assertLess(ready_path.index("adsp_glink_mode3_start()"),
                        ready_path.index("adsp_apr_mode4_start()"))
        self.assertIn("adsp_apr_open_channel", apr_start)
        self.assertLess(apr_start.index("adsp_apr_send_fwk_version"),
                        apr_start.index("adsp_apr_poll_response"))
        self.assertEqual(driver.count("beryllium_adsp_apr_encode_get_fwk_version("), 1)
        local_intents = added_function_body(driver, "adsp_apr_advertise_local_intents")
        self.assertIn("for (uint32_t id = 1; id <= BERYLLIUM_ADSP_APR_INTENT_COUNT; id++)",
                      local_intents)
        self.assertIn("ADSP_APR_LOCAL_CHANNEL", local_intents)
        self.assertIn("BERYLLIUM_ADSP_APR_INTENT_BYTES", local_intents)
        apr_transport = driver[driver.index("adsp_glink_send_frame("):
                               driver.index("beryllium_adsp_pas_start(void)")]
        self.assertIn("items->descriptor.off + 4", apr_transport)
        self.assertIn("items->descriptor.off + 8", apr_transport)
        self.assertNotRegex(apr_transport,
                            r"adsp_smem_wr32\(items->descriptor\.off \+ (?:0|12)")
        rx_consume = added_function_body(driver, "adsp_glink_rx_consume_frame")
        self.assertIn("beryllium_adsp_glink_rx_frame_available", rx_consume)
        self.assertNotIn("expected_head", rx_consume)
        self.assertIn("ADSP_APR_POLL_MS",
                      added_function_body(driver, "adsp_apr_poll_response"))

if __name__ == "__main__":
    unittest.main()
