#!/usr/bin/env python3
"""Exercise the pinned BearSSL source fallback used by the XNU staging script."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "xnu/patches-12377/0020-stage-bearssl.sh"
PIN = "7bea48e5e850ab4cafbe68d3765cdaba13a86d6f"
OFFICIAL = "https://www.bearssl.org/git/BearSSL"
MIRROR = "https://github.com/status-im/BearSSL.git"


MOCK_GIT = r'''#!/usr/bin/env python3
import os
from pathlib import Path
import sys

args = sys.argv[1:]
log = Path(os.environ["MOCK_GIT_LOG"])
with log.open("a") as stream:
    stream.write(" ".join(args) + "\n")

if args and args[0] == "init":
    raise SystemExit(0)

if len(args) >= 4 and args[0] == "-C":
    repo = Path(args[1])
    operation = args[2]
    if operation == "fetch":
        remote, pin = args[-2:]
        if remote == os.environ["MOCK_OFFICIAL"]:
            raise SystemExit(128)
        if remote == os.environ["MOCK_MIRROR"] and pin == os.environ["MOCK_PIN"]:
            files = {
                "inc/bearssl_block.h": "/* test fixture */\n",
                "src/inner.h": "#include <limits.h>\n",
                "src/config.h": "/* test fixture */\n",
                "LICENSE.txt": "test license\n",
            }
            names = (
                "symcipher/aes_ct.c", "symcipher/aes_ct_enc.c", "symcipher/aes_ct_dec.c",
                "symcipher/aes_ct_cbcenc.c", "symcipher/aes_ct_cbcdec.c",
                "hash/md5.c", "codec/dec32le.c", "codec/enc32le.c", "hash/sha1.c",
                "codec/dec32be.c", "codec/enc32be.c", "hash/sha2big.c",
                "codec/dec64be.c", "codec/enc64be.c",
            )
            files.update({"src/" + name: "/* test fixture */\n" for name in names})
            for name, content in files.items():
                path = repo / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            raise SystemExit(0)
        raise SystemExit(1)
    if operation == "checkout":
        raise SystemExit(0)
    if operation == "rev-parse":
        print(os.environ["MOCK_PIN"])
        raise SystemExit(0)

raise SystemExit(2)
'''


class BearsslStageFallbackTests(unittest.TestCase):
    def test_stages_the_pinned_source_when_the_official_host_is_unavailable(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bin_dir = root / "bin"
            work = root / "work"
            bin_dir.mkdir()
            work.mkdir()
            (work / "libkern/crypto").mkdir(parents=True)
            (work / "libkern/crypto/corecrypto_aes.c").write_text("/* source */\n")
            git = bin_dir / "git"
            git.write_text(MOCK_GIT)
            git.chmod(0o755)
            log = root / "git.log"
            env = os.environ.copy()
            env.update({
                "PATH": str(bin_dir) + os.pathsep + env["PATH"],
                "MOCK_GIT_LOG": str(log),
                "MOCK_OFFICIAL": OFFICIAL,
                "MOCK_MIRROR": MIRROR,
                "MOCK_PIN": PIN,
                "MACHINE_CONFIG": "SWEET",
            })

            result = subprocess.run(["bash", str(SCRIPT)], cwd=work, env=env,
                                    capture_output=True, text=True)

            self.assertEqual(result.returncode, 0, result.stderr)
            calls = log.read_text()
            self.assertIn(OFFICIAL, calls)
            self.assertIn(MIRROR, calls)
            self.assertLess(calls.index(OFFICIAL), calls.index(MIRROR))
            staged = work / "libkern/crypto/sweet_bearssl"
            self.assertEqual((staged / "src/symcipher/aes_ct.c").read_text(), "/* test fixture */\n")
            self.assertIn("#if defined(KERNEL)", (staged / "src/inner.h").read_text())
            self.assertIn(PIN, result.stdout)


if __name__ == "__main__":
    unittest.main()
