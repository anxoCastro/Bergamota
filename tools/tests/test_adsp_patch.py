#!/usr/bin/env python3
"""Static regression test for the XNU ADSP preflight hook ordering."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "xnu/patches-12377/1131-beryllium-adsp-apr.patch"


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


class AdspPatchPlacementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = PATCH.read_text()

    def test_early_hook_only_takes_the_smp2p_snapshot(self):
        section = diff_section(self.patch, "bsd/dev/beryllium_adsp.c")
        body = added_function_body(section, "beryllium_adsp_probe")
        self.assertIn('PE_parse_boot_argn("sweetadsp", &mode, sizeof(mode))', body)
        self.assertIn("if (mode != 1)", body)
        self.assertIn("adsp_probe_enabled = true;", body)
        self.assertIn("adsp_smp2p_readonly_probe();", body)
        self.assertNotIn("adsp_fw_read_metadata(", body)

        firmware_probe = added_function_body(section, "beryllium_adsp_firmware_probe")
        self.assertIn("if (!adsp_probe_enabled || adsp_firmware_probe_done)", firmware_probe)
        self.assertIn("if (rootvnode == NULLVP)", firmware_probe)
        self.assertIn("adsp_fw_read_metadata(", firmware_probe)

    def test_firmware_probe_runs_after_rootvnode_is_set(self):
        section = diff_section(self.patch, "bsd/kern/bsd_init.c")
        self.assertTrue(section, "the patch has no post-root hook in bsd_init.c")
        set_root = section.index("set_rootvnode(init_rootvnode);")
        unlock = section.index("lck_rw_unlock_exclusive(&rootvnode_rw_lock);")
        probe = section.index("beryllium_adsp_firmware_probe();")
        self.assertLess(set_root, unlock)
        self.assertLess(unlock, probe)


if __name__ == "__main__":
    unittest.main()
